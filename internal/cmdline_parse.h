#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <string_view>

#include "cmdline_error.h"
#include "cmdline_rules.h"
#include "cmdline_utils.h"

// Contains the actual compile-time parser for command-line specs. This takes a
// string as a non-type template parameter (NTTP) and uses constexpr-only
// operations to parse it into an instance of ParsedSpec.
//
// The primary entry point is `parse_spec` which returns either a parsed spec or
// an error indicating where a parsing error occurred.
//
// This file only concerns itself with syntax parsing. Semantic checks are done
// in cmdline_check.h.

namespace cmd {

// Represents either a parsed value or the first error that blocked parsing.
template <typename Value>
using ParseResult = std::expected<Value, ParseError>;

// Represents status for a parse operation that doesn't return a value.
using ParseStatus = std::expected<void, ParseError>;

// Describes one positional argument parsed from the spec.
struct ArgSpec {
    TextRange name{};         // Argument name without angle brackets.
    TextRange type{};         // Optional type after ':'. Empty is string.
    TextRange description{};  // Description text.
    bool variadic{};          // Accepts trailing repeated values.
    bool required{true};      // Must receive at least one command-line value.

    // Returns whether this argument accepts zero or one value.
    constexpr bool optional() const { return !variadic && !required; }
};

// Describes one switch or value-taking option parsed from the spec.
struct OptSpec {
    TextRange long_name{};      // Long name without leading "--".
    TextRange short_name{};     // Short name without leading "-".
    TextRange value_name{};     // Explicit type name for typed values.
    TextRange choices{};        // Raw [a|b|c] value spec.
    TextRange default_value{};  // Value from [default: value].
    TextRange env_name{};       // Environment variable from [env: NAME].
    TextRange description{};    // Text before recognized option tags.
    ValueType value_type{};     // Value grammar.
    RepeatPolicy repeat{};      // Repetition policy.
    bool takes_value{};         // Consumes a following/inline value.
    bool counting{};            // Counts uses instead of bool state.
    bool required{};            // Needs argv, default, or env value.
    bool hidden{};              // Omitted from help output.
    bool default_quoted{};      // Default came from quoted syntax.

    // Returns whether every repeated value is retained.
    constexpr bool keeps_all() const { return repeat == RepeatPolicy::kAll; }

    // Returns whether a value is present in the option's choice list.
    constexpr bool choice_allows_value(
        std::string_view source, TextRange value) const {
        std::string_view text  = choices.view(source);
        std::string_view match = value.view(source);

        size_t offset = 0;
        while (offset <= text.size()) {
            size_t next = text.find('|', offset);
            if (next == std::string_view::npos) {
                next = text.size();
            }

            if (text.substr(offset, next - offset) == match) {
                return true;
            }

            if (next == text.size()) {
                break;
            }

            offset = next + 1;
        }
        return false;
    }

    // Returns whether this switch accepts the implicit '--no-' spelling.
    constexpr bool has_negative_form() const {
        return long_name && !takes_value &&
               (repeat == RepeatPolicy::kFirst ||
                repeat == RepeatPolicy::kLast);
    }

    // Returns whether `name` matches this option's implicit '--no-' form.
    constexpr bool negative_form_matches(
        std::string_view source, TextRange name) const {
        std::string_view candidate = name.view(source);
        std::string_view positive  = long_name.view(source);
        return candidate.size() == positive.size() + 3 &&
               candidate.starts_with("no-") && candidate.substr(3) == positive;
    }

    // Returns whether a scalar typed accessor should return std::optional<T>.
    constexpr bool is_optional_value() const {
        return takes_value && !keeps_all() && !required && !default_value;
    }
};

// Describes one parsed line and links it to its argument or option metadata.
struct LineSpec {
    LineType type{LineType::kBlank};  // Source line classification.
    TextRange text{};                 // Original line text.
    size_t index{};                   // ArgSpec or OptSpec index by type.
    size_t indent{};                  // Help indentation in display columns.
};

// A command line spec parsed from raw text.
template <size_t N, size_t M>
struct ParsedSpec {
    StaticText<M> source{};
    std::array<LineSpec, N> lines{};
    std::array<ArgSpec, N> args{};
    std::array<OptSpec, N> opts{};
    size_t nline = 0;
    size_t narg  = 0;
    size_t nopt  = 0;
    bool valid   = true;

    // Returns the type name for one positional argument.
    constexpr std::string_view arg_type_name(size_t index) const {
        const ArgSpec& arg = args[index];
        if (arg.type) {
            return arg.type.view(source);
        }
        return arg.name.view(source);
    }

    // Returns the type name for one option value.
    constexpr std::string_view opt_type_name(size_t index) const {
        const OptSpec& option = opts[index];
        if (option.takes_value && option.value_type == ValueType::kType) {
            return option.value_name.view(source);
        }
        return kDefaultTypeName;
    }
};

// Builds a parse error at the start of a parsed source range.
template <size_t N, size_t M>
constexpr ParseError make_error(
    const ParsedSpec<N, M>& spec, TextRange range, ParseError::Type type) {
    return {
        location_for(spec.source, static_cast<size_t>(range.begin)),
        type,
    };
}

// Builds a parse error at a pointer into the parsed spec source text.
template <size_t N, size_t M>
constexpr ParseError make_error(
    const ParsedSpec<N, M>& spec, const char* cursor, ParseError::Type type) {
    return make_error(
        spec, TextRange::From(spec.source.data(), cursor, cursor), type);
}

// Tracks one parse position through a command spec source range.
template <size_t N, size_t M>
struct SpecCursor {
    const ParsedSpec<N, M>& spec;
    const char* pos = nullptr;
    const char* end = nullptr;

    // Returns true when the cursor has consumed its whole range.
    constexpr bool done() const { return pos == end; }

    // Returns a byte ahead of the cursor, or nul when no byte remains.
    constexpr char peek(size_t offset = 0) const {
        return offset < size_t(end - pos) ? pos[offset] : '\0';
    }

    // Returns the unconsumed suffix of the cursor range.
    constexpr std::string_view rest() const {
        return std::string_view(pos, end - pos);
    }

    // Builds a source range from two pointers in the parsed spec.
    constexpr TextRange range(const char* first, const char* last) const {
        return TextRange::From(spec.source.data(), first, last);
    }

    // Builds a source range from a view into the parsed spec.
    constexpr TextRange range(std::string_view text) const {
        return TextRange::From(spec.source, text);
    }

    // Builds a parse error at the current cursor position.
    constexpr ParseError error(ParseError::Type type) const {
        return make_error(spec, pos, type);
    }

    // Builds a parse error at a specific source pointer.
    constexpr ParseError error_at(
        const char* where, ParseError::Type type) const {
        return make_error(spec, where, type);
    }

    // Consumes one character if it matches.
    constexpr bool accept(char ch) {
        if (ch == '\0' || peek() != ch) {
            return false;
        }

        ++pos;
        return true;
    }

    // Consumes a fixed string if it matches.
    constexpr bool accept(std::string_view text) {
        if (!rest().starts_with(text)) {
            return false;
        }

        pos += text.size();
        return true;
    }

    // Consumes a required character or reports the supplied error type.
    constexpr ParseStatus expect(char ch, ParseError::Type type) {
        if (accept(ch)) {
            return {};
        }

        return std::unexpected(error(type));
    }

    // Consumes horizontal whitespace.
    constexpr void skip_horizontal_space() {
        while (is_horizontal_space(peek())) {
            ++pos;
        }
    }

    // Consumes until one of the delimiter characters is reached.
    constexpr TextRange take_until(std::string_view chars) {
        const char* first = pos;
        while (peek() != '\0' && !is_oneof(peek(), chars)) {
            ++pos;
        }
        return range(first, pos);
    }
};

// Creates a parser cursor for a source view.
template <size_t N, size_t M>
constexpr SpecCursor<N, M> make_cursor(
    const ParsedSpec<N, M>& spec, std::string_view text) {
    return SpecCursor<N, M>{
        spec,
        text.data(),
        text.data() + text.size(),
    };
}

// Parses an argument, option, type, or choice identifier. Identifiers contain
// ASCII letters, digits, and '-' characters. They may start with a digit, but
// '-' may not appear first, last, or twice in a row.
//
// Valid identifiers:
//  - input
//  - 2fast
//  - output-file
//
// Invalid identifiers:
//  - -input
//  - output-
//  - input--file
//
template <size_t N, size_t M>
constexpr ParseResult<TextRange> parse_identifier(
    SpecCursor<N, M>& cursor, ParseError::Type type) {
    if (!is_identifier_head(cursor.peek())) {
        return std::unexpected(cursor.error(type));
    }

    const char* first = cursor.pos;
    ++cursor.pos;
    while (is_identifier_tail(cursor.peek())) {
        if (cursor.peek() == '-' && cursor.peek(1) == '-') {
            return std::unexpected(cursor.error_at(
                cursor.pos + 1, ParseError::kNameContainsConsecutiveDash));
        }
        ++cursor.pos;
    }

    if (*(cursor.pos - 1) == '-') {
        return std::unexpected(
            cursor.error_at(cursor.pos - 1, ParseError::kNameEndsWithDash));
    }
    return cursor.range(first, cursor.pos);
}

// Checks that a choice list contains only non-empty identifiers.
template <size_t N, size_t M>
constexpr ParseStatus check_choice_values(
    const ParsedSpec<N, M>& spec, TextRange choices) {
    std::string_view text = choices.view(spec.source);
    size_t offset         = 0;
    while (offset <= text.size()) {
        size_t next = text.find('|', offset);
        if (next == std::string_view::npos) {
            next = text.size();
        }

        // Two separators in a row, e.g. || is an error.
        if (offset == next) {
            return std::unexpected(make_error(
                spec, TextRange{choices.begin + static_cast<int>(offset), 0},
                ParseError::kEmptyChoiceValue));
        }

        if (!is_identifier_head(text[offset])) {
            return std::unexpected(make_error(
                spec, TextRange{choices.begin + static_cast<int>(offset), 0},
                ParseError::kInvalidChoiceValue));
        }

        for (size_t i = offset + 1; i < next; ++i) {
            if (!is_identifier_tail(text[i])) {
                return std::unexpected(make_error(
                    spec, TextRange{choices.begin + static_cast<int>(i), 0},
                    ParseError::kInvalidChoiceValue));
            }
        }

        if (next == text.size()) {
            break;
        }

        offset = next + 1;
    }
    return {};
}

// Parses a spec line containing a positional argument spec.
template <size_t N, size_t M>
constexpr ParseResult<ArgSpec> parse_arg_line(
    const ParsedSpec<N, M>& spec, std::string_view line) {
    SpecCursor<N, M> cursor = make_cursor(spec, line);

    if (auto status = cursor.expect('<', ParseError::kExpectedLess); !status) {
        return std::unexpected(status.error());
    }

    auto name = parse_identifier(cursor, ParseError::kExpectedArgumentName);
    if (!name) {
        return std::unexpected(name.error());
    }

    TextRange type;
    if (cursor.accept(':')) {
        auto parsed = parse_identifier(cursor, ParseError::kExpectedTypeName);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        type = *parsed;
    }

    if (auto status = cursor.expect('>', ParseError::kExpectedValueClose);
        !status) {
        return std::unexpected(status.error());
    }

    bool variadic = false;
    bool required = true;
    if (cursor.accept('+')) {
        variadic = true;
    } else if (cursor.accept('*')) {
        variadic = true;
        required = false;
    } else if (cursor.accept('?')) {
        required = false;
    }

    if (is_oneof(cursor.peek(), "+*?")) {
        return std::unexpected(
            cursor.error(ParseError::kDuplicateRepeatMarker));
    }

    return ArgSpec{
        *name, type, cursor.range(trim(cursor.rest())), variadic, required,
    };
}

// Parses the optional value spec for a switch or option.
template <size_t N, size_t M>
constexpr ParseStatus parse_value_spec(
    SpecCursor<N, M>& cursor, OptSpec& option) {
    if (!cursor.accept('=')) {
        return {};
    }

    // Value spec is being given. If the next byte is not '<' or '[', the option
    // takes an unconstrained string and the remaining text is description.
    option.takes_value = true;
    if (cursor.peek() == '\0' || is_horizontal_space(cursor.peek())) {
        option.value_type = ValueType::kString;
        return {};
    }

    // Parse optional type name.
    if (cursor.accept('<')) {
        auto type = parse_identifier(cursor, ParseError::kExpectedTypeName);
        if (!type) {
            return std::unexpected(type.error());
        }

        ParseStatus closed =
            cursor.expect('>', ParseError::kExpectedValueClose);
        if (!closed) {
            return std::unexpected(closed.error());
        }

        option.value_type = ValueType::kType;
        option.value_name = *type;
        return {};
    }

    // Parse optional choice-list values.
    if (cursor.accept('[')) {
        const char* value_begin = cursor.pos;
        TextRange choices       = cursor.take_until("]");

        if (cursor.done()) {
            return std::unexpected(
                cursor.error_at(value_begin, ParseError::kExpectedChoiceClose));
        }

        option.value_type   = ValueType::kChoice;
        option.choices      = choices;
        ParseStatus checked = check_choice_values(cursor.spec, option.choices);
        if (!checked) {
            return std::unexpected(checked.error());
        }

        cursor.accept(']');
        return {};
    }

    return std::unexpected(cursor.error(ParseError::kExpectedTypeOrChoices));
}

// Parses recognized trailing option tags and applies them to one option.
template <size_t N, size_t M>
struct TagParser {
    const ParsedSpec<N, M>& spec;
    SpecCursor<N, M>& cursor;
    OptSpec& option;

    // Stores one parsed default value and whether it used quoted syntax.
    struct DefaultValue {
        TextRange value{};
        bool quoted{};
    };

    // Returns the first horizontal space inside one range, if any.
    constexpr const char* first_horizontal_space(TextRange value) const {
        std::string_view text = value.view(spec.source);

        for (size_t i = 0; i < text.size(); ++i) {
            if (is_horizontal_space(text[i])) {
                return spec.source.data() + value.begin + i;
            }
        }
        return nullptr;
    }

    // Normalizes one default-tag value and validates its quoting rules.
    constexpr ParseResult<TextRange> parse_default_value(
        TextRange raw, const char* value_begin) const {
        TextRange value = raw.trimmed(spec.source);
        if (value.empty()) {
            return std::unexpected(
                make_error(spec, value_begin, ParseError::kEmptyDefaultValue));
        }

        std::string_view text = value.view(spec.source);
        bool quoted =
            text.size() >= 2 && text.front() == '"' && text.back() == '"';
        if (text.front() == '"' || text.back() == '"') {
            if (!quoted) {
                return std::unexpected(make_error(
                    spec, value, ParseError::kMisquotedDefaultValue));
            }

            if (value.size == 2) {
                return std::unexpected(make_error(
                    spec, TextRange{value.begin + 1, 0},
                    ParseError::kEmptyDefaultValue));
            }

            TextRange inner{value.begin + 1, value.size - 2};
            return inner;
        }

        if (const char* space = first_horizontal_space(value)) {
            return std::unexpected(
                make_error(spec, space, ParseError::kMisquotedDefaultValue));
        }
        return value;
    }

    // Parses a quoted default-tag value and validates supported escapes.
    constexpr ParseResult<TextRange> parse_quoted_default_range() const {
        const char* first = cursor.pos;
        ++cursor.pos;
        while (cursor.peek() != '\0' && cursor.peek() != '"') {
            if (cursor.peek() == ']') {
                return std::unexpected(make_error(
                    spec, first, ParseError::kMisquotedDefaultValue));
            }

            if (cursor.peek() == '\\') {
                if (cursor.peek(1) != '"' && cursor.peek(1) != '\\') {
                    return std::unexpected(
                        cursor.error(ParseError::kMisquotedDefaultValue));
                }

                cursor.pos += 2;
                continue;
            }

            ++cursor.pos;
        }

        if (!cursor.accept('"')) {
            return std::unexpected(
                make_error(spec, first, ParseError::kMisquotedDefaultValue));
        }

        const char* last = cursor.pos;
        cursor.skip_horizontal_space();
        if (cursor.peek() != ']') {
            return std::unexpected(
                cursor.error(ParseError::kExpectedChoiceClose));
        }

        return TextRange::From(spec.source.data(), first, last);
    }

    // Parses one default-tag value after its ':'.
    constexpr ParseResult<DefaultValue> parse_default_tag_value(
        const char* value_begin) const {
        TextRange value;
        bool quoted = cursor.peek() == '"';
        if (quoted) {
            ParseResult<TextRange> parsed = parse_quoted_default_range();
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            value = *parsed;
        } else {
            value = cursor.take_until("]");
        }

        ParseResult<TextRange> parsed = parse_default_value(value, value_begin);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        return DefaultValue{*parsed, quoted};
    }

    // Validates one environment-variable tag value.
    constexpr ParseResult<TextRange> parse_env_value(
        TextRange value, const char* value_begin) const {
        if (trim(value.view(spec.source)).empty()) {
            return std::unexpected(
                make_error(spec, value_begin, ParseError::kEmptyEnvName));
        }

        if (const char* space = first_horizontal_space(value)) {
            return std::unexpected(
                make_error(spec, space, ParseError::kInvalidEnvName));
        }

        std::string_view text = value.view(spec.source);
        if (!is_env_name_head(text.front())) {
            return std::unexpected(make_error(
                spec, TextRange{value.begin, 0}, ParseError::kInvalidEnvName));
        }

        std::string_view tail = value.view(spec.source, 1);
        for (size_t i = 0; i < tail.size(); ++i) {
            if (!is_env_name_tail(tail[i])) {
                return std::unexpected(make_error(
                    spec, TextRange{value.begin + 1 + static_cast<int>(i), 0},
                    ParseError::kInvalidEnvName));
            }
        }
        return value;
    }

    // Applies one parsed default tag to the current option.
    constexpr ParseStatus apply_default_tag(
        DefaultValue value, const char* key_begin) const {
        if (option.default_value) {
            return std::unexpected(
                make_error(spec, key_begin, ParseError::kDuplicateTag));
        }

        option.default_value  = value.value;
        option.default_quoted = value.quoted;
        return {};
    }

    // Applies one recognized non-default '[key: value]' tag to the option.
    constexpr ParseResult<bool> apply_value_tag(
        std::string_view key, TextRange value, const char* key_begin,
        const char* value_begin) const {
        if (key == "env") {
            if (option.env_name) {
                return std::unexpected(
                    make_error(spec, key_begin, ParseError::kDuplicateTag));
            }

            ParseResult<TextRange> parsed = parse_env_value(value, value_begin);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }

            option.env_name = *parsed;
            return true;
        }

        return false;
    }

    // Applies one recognized '[key]' flag tag to the current option.
    constexpr ParseResult<bool> apply_flag_tag(
        std::string_view key, const char* key_begin) const {
        if (key == "required") {
            if (option.required) {
                return std::unexpected(
                    make_error(spec, key_begin, ParseError::kDuplicateTag));
            }
            option.required = true;
            return true;
        }

        if (key == "hidden") {
            if (option.hidden) {
                return std::unexpected(
                    make_error(spec, key_begin, ParseError::kDuplicateTag));
            }
            option.hidden = true;
            return true;
        }

        return false;
    }

    // Parses one bracketed option tag after the opening '['.
    constexpr ParseResult<const char*> parse_tag() {
        const char* key_begin = cursor.pos;
        cursor.take_until("]:");

        std::string_view key(key_begin, cursor.pos - key_begin);
        if (key == "alias") {
            return std::unexpected(
                cursor.error_at(key_begin, ParseError::kUnsupportedAlias));
        }

        if (cursor.accept(':')) {
            cursor.skip_horizontal_space();
            const char* value_begin = cursor.pos;

            if (key == "default") {
                auto value = parse_default_tag_value(value_begin);
                if (!value) {
                    return std::unexpected(value.error());
                }

                ParseStatus applied = apply_default_tag(*value, key_begin);
                if (!applied) {
                    return std::unexpected(applied.error());
                }

            } else {
                TextRange value = cursor.take_until("]");
                if (cursor.done()) {
                    return std::unexpected(cursor.error_at(
                        value_begin, ParseError::kExpectedChoiceClose));
                }

                auto applied =
                    apply_value_tag(key, value, key_begin, value_begin);
                if (!applied) {
                    return std::unexpected(applied.error());
                }

                if (!*applied) {
                    return std::unexpected(make_error(
                        spec, key_begin, ParseError::kUnknownOptionTag));
                }
            }

        } else {
            if (cursor.peek() != ']') {
                return std::unexpected(
                    cursor.error(ParseError::kExpectedChoiceClose));
            }

            auto applied = apply_flag_tag(key, key_begin);

            if (!applied) {
                return std::unexpected(applied.error());
            }

            if (!*applied) {
                return std::unexpected(
                    make_error(spec, key_begin, ParseError::kUnknownOptionTag));
            }
        }

        ParseStatus closed =
            cursor.expect(']', ParseError::kExpectedChoiceClose);
        if (!closed) {
            return std::unexpected(closed.error());
        }
        return key_begin;
    }
};

// Parses bracketed option tags and separates them from description text.
template <size_t N, size_t M>
constexpr ParseStatus parse_tags(
    const ParsedSpec<N, M>& spec, OptSpec& option, std::string_view tail) {
    SpecCursor<N, M> cursor     = make_cursor(spec, tail);
    const char* description_end = cursor.end;

    // Supported tags are '[default: value]', '[env: NAME]', '[required]', and
    // '[hidden]'. Parsing them here keeps the option description as the text
    // before the first recognized trailing tag.
    TagParser<N, M> parser{spec, cursor, option};

    // Advances over description text without treating escaped brackets as tags.
    const auto take_until_tag_open = [&]() {
        while (cursor.peek() != '\0') {
            if (cursor.peek() == '\\' && is_oneof(cursor.peek(1), "[]")) {
                cursor.pos += 2;
                continue;
            }

            if (cursor.peek() == '[') {
                break;
            }

            ++cursor.pos;
        }
    };

    bool seen_tag = false;
    while (cursor.peek() != '\0') {
        cursor.skip_horizontal_space();
        if (cursor.done()) {
            break;
        }

        if (!cursor.accept('[')) {
            if (seen_tag) {
                return std::unexpected(
                    cursor.error(ParseError::kTextAfterOptionTag));
            }

            take_until_tag_open();
            continue;
        }

        ParseResult<const char*> parsed_tag = parser.parse_tag();
        if (!parsed_tag) {
            return std::unexpected(parsed_tag.error());
        }

        // The description stops just before the first recognized trailing tag.
        if (description_end == cursor.end) {
            description_end = *parsed_tag - 1;
        }
        seen_tag = true;
    }

    std::string_view description =
        trim(tail.substr(0, description_end - tail.data()));
    option.description = TextRange::From(spec.source, description);
    return {};
}

// Parses a spec line containing an option spec.
template <size_t N, size_t M>
constexpr ParseResult<OptSpec> parse_opt_line(
    const ParsedSpec<N, M>& spec, std::string_view line) {

    SpecCursor<N, M> cursor = make_cursor(spec, line);
    OptSpec option;

    // Returns true if the character starts the value, repeat, or description.
    const auto is_tail_delimiter = [](char c) {
        return c == '\0' || is_horizontal_space(c) || is_oneof(c, "=+<>");
    };

    // Validates that the parsed name is followed by a legal tail token.
    const auto expect_tail_delimiter = [&]() -> ParseStatus {
        if (!is_tail_delimiter(cursor.peek())) {
            return std::unexpected(
                cursor.error(ParseError::kExpectedOptionDelimiter));
        }
        return {};
    };

    // Parses a short option name, either standalone or after a long name.
    const auto parse_short = [&]() -> ParseStatus {
        if (cursor.peek() != '-' || !is_identifier_head(cursor.peek(1))) {
            const char* where =
                cursor.peek() == '-' ? cursor.pos + 1 : cursor.pos;
            return std::unexpected(
                cursor.error_at(where, ParseError::kExpectedShortOption));
        }

        cursor.pos += 2;
        option.short_name = cursor.range(cursor.pos - 1, cursor.pos);
        return expect_tail_delimiter();
    };

    // Parses a long option name and an optional '/-s' short name.
    const auto parse_long = [&]() -> ParseStatus {
        if (!cursor.accept("--")) {
            return std::unexpected(cursor.error(ParseError::kExpectedOption));
        }

        const char* name_begin = cursor.pos;
        auto name = parse_identifier(cursor, ParseError::kExpectedOptionName);
        if (!name) {
            return std::unexpected(name.error());
        }

        if (name->size < 2) {
            return std::unexpected(make_error(
                spec, name_begin, ParseError::kLongOptionNameTooShort));
        }

        option.long_name = *name;
        if (cursor.accept('/')) {
            return parse_short();
        }

        return expect_tail_delimiter();
    };

    // Parses the supported option-name forms: '--long', '--long/-s', or '-s'.
    const auto parse_names = [&]() -> ParseStatus {
        if (cursor.rest().starts_with("--")) {
            return parse_long();
        }

        if (cursor.peek() == '-') {
            return parse_short();
        }

        return std::unexpected(cursor.error(ParseError::kExpectedOption));
    };

    // Parses the optional value grammar after the option names.
    const auto parse_value = [&]() -> ParseStatus {
        return parse_value_spec(cursor, option);
    };

    // Parses one optional repeat policy marker.
    const auto parse_repeat = [&]() -> ParseStatus {
        if (cursor.accept('+')) {
            option.repeat   = RepeatPolicy::kAll;
            option.counting = !option.takes_value;
        } else if (cursor.accept('<')) {
            option.repeat = RepeatPolicy::kFirst;
        } else if (cursor.accept('>')) {
            option.repeat = RepeatPolicy::kLast;
        }

        if (is_oneof(cursor.peek(), "+<>")) {
            return std::unexpected(make_error(
                spec, cursor.pos, ParseError::kDuplicateRepeatMarker));
        }

        return {};
    };

    // Parses the description and any trailing option tags.
    const auto parse_description = [&]() -> ParseStatus {
        return parse_tags(spec, option, cursor.rest());
    };

    ParseStatus parsed = parse_names()
                             .and_then(parse_value)
                             .and_then(parse_repeat)
                             .and_then(parse_description);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return option;
}

// Parses a full command spec into syntax metadata.
template <
    FixedString kSpecText,
    size_t N = count_lines(trim_source(kSpecText.view())),
    size_t M = trim_source(kSpecText.view()).size()>
constexpr ParseResult<ParsedSpec<N, M>> parse_spec() {
    constexpr std::string_view source = trim_source(kSpecText.view());
    ParsedSpec<N, M> spec{
        .source = StaticText<M>(source),
    };

    // Adds a source line to the parsed spec.
    const auto add_line = [&spec](
                              LineType type, TextRange text, size_t index = 0,
                              size_t line_indent = 0) -> ParseStatus {
        if (spec.nline >= N) {
            return std::unexpected(
                make_error(spec, text, ParseError::kTooManyLines));
        }

        spec.lines[spec.nline++] = LineSpec{
            type,
            text,
            index,
            line_indent,
        };

        return {};
    };

    // Adds a positional argument to the spec and returns its index.
    const auto add_arg =
        [&spec](std::string_view line, ArgSpec arg) -> ParseResult<size_t> {
        if (spec.narg >= N) {
            return std::unexpected(make_error(
                spec, TextRange::From(spec.source, line),
                ParseError::kTooManyArguments));
        }

        size_t index     = spec.narg++;
        spec.args[index] = arg;
        return index;
    };

    // Adds an option to the spec and returns its index.
    const auto add_opt =
        [&spec](std::string_view line, OptSpec option) -> ParseResult<size_t> {
        if (spec.nopt >= N) {
            return std::unexpected(make_error(
                spec, TextRange::From(spec.source, line),
                ParseError::kTooManyOptions));
        }

        size_t index     = spec.nopt++;
        spec.opts[index] = option;
        return index;
    };

    // Creates a source range from the current line view.
    auto range = [&spec](std::string_view line_text) {
        return TextRange::From(spec.source, line_text);
    };

    size_t indent = common_indent(source);
    size_t offset = 0;

    while (!spec.source.empty() && offset <= spec.source.size()) {
        size_t line_end_offset = line_end_from_offset(spec.source, offset);

        std::string_view line_text(
            spec.source.data() + offset, line_end_offset - offset);
        if (line_text.size() >= indent) {
            line_text.remove_prefix(indent);
        }

        size_t line_indent         = leading_indent(line_text);
        std::string_view line_body = trim(line_text);
        if (line_body.empty()) {
            if (auto status = add_line(
                    LineType::kBlank, TextRange{static_cast<int>(offset), 0});
                !status) {
                return std::unexpected(status.error());
            }

        } else if (line_body.front() == '_') {
            line_body.remove_prefix(1);
            line_body = trim(line_body);
            if (auto status = add_line(
                    LineType::kHeading, range(line_body), 0, line_indent);
                !status) {
                return std::unexpected(status.error());
            }

        } else if (line_body.front() == '<') {
            auto parsed = parse_arg_line(spec, line_body);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }

            auto index = add_arg(line_body, *parsed);
            if (!index) {
                return std::unexpected(index.error());
            }

            ParseStatus status =
                add_line(LineType::kArg, range(line_body), *index, line_indent);
            if (!status) {
                return std::unexpected(status.error());
            }

        } else if (line_body.front() == '-') {
            auto parsed = parse_opt_line(spec, line_body);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }

            auto index = add_opt(line_body, *parsed);
            if (!index) {
                return std::unexpected(index.error());
            }

            ParseStatus status = add_line(
                LineType::kOption, range(line_body), *index, line_indent);
            if (!status) {
                return std::unexpected(status.error());
            }

        } else {
            if (auto status = add_line(
                    LineType::kRawText, range(line_body), 0, line_indent);
                !status) {
                return std::unexpected(status.error());
            }
        }

        if (line_end_offset == spec.source.size()) {
            break;
        }
        offset = line_end_offset + 1;
    }

    return spec;
}

}  // namespace cmd
