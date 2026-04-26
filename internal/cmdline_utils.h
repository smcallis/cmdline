#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// Provides small standalone constexpr helpers shared by the parser, checker,
// diagnostics, and runtime.

namespace cmd {

// Stores a string literal as a structural type for template arguments.
template <size_t kSize>
struct FixedString {
    char value[kSize]{};

    constexpr FixedString() = default;

    constexpr FixedString(const char (&text)[kSize]) {
        // memcpy isn't compatible with constexpr.
        for (size_t index = 0; index < kSize; ++index) {
            value[index] = text[index];
        }
    }

    constexpr std::string_view view() const { return value; }
};

// Owns constexpr text so parsed specs can be structural template arguments.
template <size_t N>
struct StaticText {
    std::array<char, N> value{};

    constexpr StaticText() = default;

    constexpr explicit StaticText(std::string_view text) {
        // memcpy isn't compatible with constexpr.
        for (size_t index = 0; index < N && index < text.size(); ++index) {
            value[index] = text[index];
        }
    }

    constexpr const char* data() const { return value.data(); }
    constexpr size_t size() const { return N; }
    constexpr bool empty() const { return N == 0; }
    constexpr char operator[](size_t index) const { return value[index]; }
    constexpr std::string_view view() const { return {data(), size()}; }

    constexpr operator std::string_view() const { return view(); }
    constexpr bool operator==(const StaticText& other) const = default;
};

// Owns fixed-capacity constexpr text assembled incrementally.
template <size_t N>
struct StaticString {
    std::array<char, N> value{};
    size_t length{};

    constexpr StaticString() = default;

    template <size_t M>
    constexpr StaticString(const char (&text)[M]) {
        for (size_t index = 0; index + 1 < M; ++index) {
            append(text[index]);
        }
    }

    constexpr void append(char ch) {
        if (length < N) {
            value[length++] = ch;
        }
    }

    constexpr void append(std::string_view text) {
        for (char ch : text) {
            append(ch);
        }
    }

    constexpr void append(size_t number) {
        char digits[20]{};
        size_t ndigit = 0;
        do {
            digits[ndigit++] = '0' + (number % 10);
            number /= 10;
        } while (number != 0);

        while (ndigit > 0) {
            append(digits[--ndigit]);
        }
    }

    // Appends several pieces of text in order.
    template <typename First, typename Second, typename... Rest>
    constexpr void append(
        const First& first, const Second& second, const Rest&... rest) {
        append(first);
        append(second);
        (append(rest), ...);
    }

    constexpr const char* data() const { return value.data(); }
    constexpr size_t size() const { return length; }
    constexpr bool empty() const { return length == 0; }
    constexpr std::string_view view() const { return {data(), size()}; }
};

// References a byte range in a source string without copying it. This uses an
// in-band empty state instead of std::optional because parsed specs are used as
// structural template arguments and std::optional is not structural.
struct TextRange {
    static constexpr int kNothing = -1;

    int begin = kNothing;
    int size  = 0;

    // Creates a source-relative range from two pointers into the same source.
    static constexpr TextRange From(
        const char* source, const char* begin, const char* end) {
        return {
            static_cast<int>(begin - source),
            static_cast<int>(end - begin),
        };
    }

    // Creates a source-relative range from a view into source.
    static constexpr TextRange From(
        std::string_view source, std::string_view text) {
        return From(source.data(), text.data(), text.data() + text.size());
    }

    constexpr bool has_value() const { return begin != kNothing; }
    constexpr bool empty() const { return !has_value() || size == 0; }
    constexpr int end() const { return has_value() ? begin + size : kNothing; }
    constexpr explicit operator bool() const { return has_value(); }

    // Returns this range after trimming horizontal whitespace.
    constexpr TextRange trimmed(std::string_view source) const;

    // Returns an actual string view given the source text, optionally starting
    // at an offset within the range.
    constexpr std::string_view view(
        std::string_view source, int offset = 0) const {
        if (!has_value() || offset < 0 || offset > size) {
            return {};
        }
        return source.substr(
            static_cast<size_t>(begin + offset),
            static_cast<size_t>(size - offset));
    }

    // Compares ranges by position and length, not by referenced text.
    constexpr bool operator==(const TextRange& other) const = default;
};

// Stores a small fixed-capacity list of source ranges.
template <size_t kCapacity>
struct TextRangeList {
    std::array<TextRange, kCapacity> values{};
    size_t count = 0;

    constexpr bool operator==(const TextRangeList& other) const = default;
};

// Splits a range on commas when requested, preserving empty segments.
template <size_t kCapacity>
constexpr TextRangeList<kCapacity> maybe_split(
    std::string_view source, TextRange value, bool split) {
    TextRangeList<kCapacity> ranges;
    if (!split) {
        ranges.values[ranges.count++] = value;
        return ranges;
    }

    int begin = value.begin;
    int end   = value.end();
    for (int i = begin; i < end; ++i) {
        if (source[static_cast<size_t>(i)] == ',') {
            ranges.values[ranges.count++] = TextRange{begin, i - begin};
            begin                         = i + 1;
        }
    }

    ranges.values[ranges.count++] = TextRange{begin, end - begin};
    return ranges;
}

// Identifies a 1-based line and column within a source string.
struct SourceLocation {
    size_t line   = 1;
    size_t column = 1;
};

// Identifies a contiguous 1-based line range in a source string.
struct SourceLineRange {
    size_t first = 1;
    size_t last  = 0;
};

// Returns true for whitespace that is significant to the spec grammar.
constexpr bool is_horizontal_space(char ch) {
    return ch == ' ' || ch == '\t';
}

// Returns true for ASCII letters accepted in identifiers.
constexpr bool is_alpha(char ch) {
    return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z');
}

constexpr bool is_digit(char ch) {
    return '0' <= ch && ch <= '9';
}

// Returns true for the first character of an argument or option name.
constexpr bool is_identifier_head(char ch) {
    return is_alpha(ch) || is_digit(ch);
}

// Returns true for non-leading characters in argument or option names.
constexpr bool is_identifier_tail(char ch) {
    return is_identifier_head(ch) || ch == '-';
}

// Returns true for the first character in an environment variable name.
constexpr bool is_env_name_head(char ch) {
    return is_alpha(ch) || ch == '_';
}

// Returns true for non-leading characters in environment variable names.
constexpr bool is_env_name_tail(char ch) {
    return is_alpha(ch) || is_digit(ch) || ch == '_';
}

// Returns true when a character is one of a given set.
constexpr bool is_oneof(char c, std::string_view text) {
    return text.find(c) != std::string_view::npos;
}

// Counts spaces and tabs before the first non-space character.
constexpr size_t leading_indent(std::string_view text) {
    size_t count = 0;
    while (count < text.size() && is_horizontal_space(text[count])) {
        ++count;
    }
    return count;
}

// Returns true when a line has no user-visible content.
constexpr bool is_blank(std::string_view text) {
    for (char ch : text) {
        if (!is_horizontal_space(ch)) {
            return false;
        }
    }
    return true;
}

// Finds the end offset for the source line starting at a byte offset.
constexpr size_t line_end_from_offset(std::string_view text, size_t offset) {
    size_t next = text.find('\n', offset);
    return next == std::string_view::npos ? text.size() : next;
}

// Computes the indentation shared by every non-blank line in a block of text.
constexpr size_t common_indent(std::string_view text) {
    bool found    = false;
    size_t best   = text.size();
    size_t offset = 0;
    while (offset <= text.size()) {
        size_t next = line_end_from_offset(text, offset);

        std::string_view line(text.data() + offset, next - offset);
        if (!is_blank(line)) {
            size_t indent = leading_indent(line);
            found         = true;
            if (indent < best) {
                best = indent;
            }
        }

        if (next == text.size()) {
            break;
        }

        offset = next + 1;
    }
    return found ? best : 0;
}

// Removes leading horizontal whitespace.
constexpr std::string_view trim_left(std::string_view text) {
    size_t offset = 0;
    while (offset < text.size() && is_horizontal_space(text[offset])) {
        ++offset;
    }
    return text.substr(offset);
}

// Removes leading and trailing horizontal whitespace.
constexpr std::string_view trim(std::string_view text) {
    text = trim_left(text);
    while (!text.empty() && is_horizontal_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

// Returns this range after trimming horizontal whitespace.
constexpr TextRange TextRange::trimmed(std::string_view source) const {
    return TextRange::From(source, trim(view(source)));
}

// Removes one leading empty or blank line from a spec string.
constexpr std::string_view trim_source(std::string_view source) {
    size_t offset = 0;
    while (offset < source.size() && source[offset] != '\n') {
        if (!is_horizontal_space(source[offset])) {
            return source;
        }
        ++offset;
    }

    if (offset == source.size()) {
        return {};
    }

    return source.substr(offset + 1);
}

// Returns the size of one spec after normalizing it by stripping its own
// initial blank line and removing common leading indentation.
constexpr size_t normalized_spec_size(std::string_view source) {
    source = trim_source(source);
    if (source.empty()) {
        return 0;
    }

    size_t indent = common_indent(source);
    size_t size   = 0;
    size_t offset = 0;
    while (offset <= source.size()) {
        size_t next = line_end_from_offset(source, offset);

        std::string_view line(source.data() + offset, next - offset);
        if (line.size() >= indent) {
            line.remove_prefix(indent);
        }
        size += line.size();

        if (next == source.size()) {
            break;
        }

        ++size;
        offset = next + 1;
    }
    return size;
}

// Returns whether one normalized spec ends with a newline.
constexpr bool normalized_spec_ends_with_newline(std::string_view source) {
    source = trim_source(source);
    return !source.empty() && source.back() == '\n';
}

// Appends one spec after normalizing it by removing common leading indentation.
template <size_t N>
constexpr void append_normalized_spec(
    FixedString<N>& output, size_t& position, std::string_view source) {
    source = trim_source(source);
    if (source.empty()) {
        return;
    }

    size_t indent = common_indent(source);
    size_t offset = 0;
    while (offset <= source.size()) {
        size_t next = line_end_from_offset(source, offset);

        std::string_view line(source.data() + offset, next - offset);
        if (line.size() >= indent) {
            line.remove_prefix(indent);
        }

        for (char ch : line) {
            output.value[position++] = ch;
        }

        if (next == source.size()) {
            break;
        }

        output.value[position++] = '\n';
        offset                   = next + 1;
    }
}

// Joins two command specs after normalizing each side independently. Here
// normalizing means stripping the initial blank line and removing leading
// indentation shared by the spec's non-blank lines.
template <FixedString kFirst, FixedString kSecond>
consteval auto join_specs() {
    constexpr size_t kFirstSize  = normalized_spec_size(kFirst.view());
    constexpr size_t kSecondSize = normalized_spec_size(kSecond.view());
    constexpr size_t kSeparator =
        kFirstSize != 0 && kSecondSize != 0 &&
                !normalized_spec_ends_with_newline(kFirst.view())
            ? 1
            : 0;

    FixedString<kFirstSize + kSeparator + kSecondSize + 1> output;
    size_t position = 0;
    append_normalized_spec(output, position, kFirst.view());
    if constexpr (kSeparator != 0) {
        output.value[position++] = '\n';
    }
    append_normalized_spec(output, position, kSecond.view());
    output.value[position] = '\0';
    return output;
}

// Counts source lines, including the trailing empty line after a final newline.
constexpr size_t count_lines(std::string_view source) {
    if (source.empty()) {
        return 0;
    }

    size_t count = 1;
    for (char ch : source) {
        if (ch == '\n') {
            ++count;
        }
    }
    return count;
}

// Converts a byte offset into a source location for diagnostics.
constexpr SourceLocation location_for(std::string_view text, size_t offset) {
    SourceLocation location;
    for (size_t index = 0; index < offset && index < text.size(); ++index) {
        if (text[index] == '\n') {
            ++location.line;
            location.column = 1;
        } else {
            ++location.column;
        }
    }
    return location;
}

// Returns the byte offset where a 1-based source line starts.
constexpr size_t line_begin(std::string_view source, size_t line) {
    if (line <= 1) {
        return 0;
    }

    size_t current = 1;
    for (size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '\n') {
            ++current;
            if (current == line) {
                return index + 1;
            }
        }
    }
    return source.size();
}

// Returns the byte offset one past the last character on a 1-based source line.
constexpr size_t line_end(std::string_view source, size_t line) {
    size_t begin = line_begin(source, line);
    size_t end   = begin;
    while (end < source.size() && source[end] != '\n') {
        ++end;
    }
    return end;
}

// Returns the source lines to show around one diagnostic location.
constexpr SourceLineRange source_line_context(
    size_t line, size_t line_count, size_t max_lines) {
    if (line_count == 0 || max_lines == 0) {
        return {};
    }

    if (line == 0) {
        line = 1;
    }
    if (line > line_count) {
        line = line_count;
    }

    size_t before = max_lines / 2;
    size_t first  = line > before ? line - before : 1;
    if (first + max_lines - 1 > line_count) {
        first = line_count > max_lines ? line_count - max_lines + 1 : 1;
    }

    size_t last = first + max_lines - 1;
    if (last > line_count) {
        last = line_count;
    }

    return {first, last};
}

}  // namespace cmd
