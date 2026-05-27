#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/cmdline_error.h"

// Defines runtime file helpers used by argv parsing. Response file expansion
// follows GCC semantics: an argv token starting with '@' is replaced by tokens
// read from that file, nested response files are expanded recursively, and an
// unreadable file leaves the original token unchanged.

namespace cmd::detail {

// Stores one argv token after response-file expansion.
struct ResponseToken {
    std::string value;
    ValueLocation location;
};

// Tracks the current source position while tokenizing a response file.
struct ResponseCursor {
    std::string_view source;
    std::string_view name;
    size_t offset = 0;
    size_t line   = 1;
    size_t column = 0;
    size_t start  = 0;

    // Returns whether all source text has been consumed.
    bool done() const { return offset >= source.size(); }

    // Returns the current source character, or '\0' at end of file.
    char peek() const {
        if (done()) {
            return '\0';
        }
        return source[offset];
    }

    // Returns the current full line without its trailing newline.
    std::string_view line_text() const {
        size_t end = source.find('\n', start);
        if (end == std::string_view::npos) {
            end = source.size();
        }

        if (end > start && source[end - 1] == '\r') {
            --end;
        }
        return source.substr(start, end - start);
    }

    // Advances the cursor by one character while tracking line and column.
    char take() {
        char ch = peek();
        if (done()) {
            return '\0';
        }

        ++offset;
        if (ch == '\n') {
            ++line;
            column = 0;
            start  = offset;
        } else {
            ++column;
        }
        return ch;
    }
};

// Returns true when a response file character separates tokens.
inline bool is_response_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' ||
           ch == '\f';
}

// Reads an entire response file, or returns empty when the file is unreadable.
inline std::optional<std::string> read_response_file(std::string_view path) {
    std::string filename(path);
    FILE* file = std::fopen(filename.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }

    std::string text;
    std::array<char, 4096> buffer{};
    while (size_t count = std::fread(buffer.data(), 1, buffer.size(), file)) {
        text.append(buffer.data(), count);
    }

    bool ok = std::ferror(file) == 0;
    std::fclose(file);
    if (!ok) {
        return {};
    }
    return text;
}

// Skips whitespace between response file tokens.
inline void skip_response_space(ResponseCursor& cursor) {
    while (!cursor.done() && is_response_space(cursor.peek())) {
        cursor.take();
    }
}

// Parses one response-file token using GCC-style quote and backslash rules.
inline std::optional<ResponseToken> parse_response_token(
    ResponseCursor& cursor) {
    skip_response_space(cursor);
    if (cursor.done()) {
        return {};
    }

    ResponseToken token{
        .location = ValueLocation::response_file(
            cursor.name, cursor.line, cursor.line_text(), cursor.column),
    };
    while (!cursor.done()) {
        char ch = cursor.peek();
        if (is_response_space(ch)) {
            break;
        }

        if (ch == '\'' || ch == '"') {
            char quote = cursor.take();
            while (!cursor.done()) {
                ch = cursor.take();
                if (ch == quote) {
                    break;
                }

                if (ch == '\\' && !cursor.done()) {
                    ch = cursor.take();
                }
                token.value.push_back(ch);
            }
            continue;
        }

        ch = cursor.take();
        if (ch == '\\' && !cursor.done()) {
            ch = cursor.take();
        }
        token.value.push_back(ch);
    }
    return token;
}

inline constexpr int kMaxResponseDepth = 32;

// Expands one token, recursing into response files when the token starts '@'.
inline void append_expanded_response_token(
    std::vector<ResponseToken>& output, ResponseToken token, int depth);

// Tokenizes and expands a response file's contents.
inline void append_response_file_tokens(
    std::vector<ResponseToken>& output, std::string_view name,
    std::string_view text, int depth) {
    ResponseCursor cursor{
        .source = text,
        .name   = name,
    };

    while (std::optional<ResponseToken> token = parse_response_token(cursor)) {
        append_expanded_response_token(output, std::move(*token), depth + 1);
    }
}

// Expands one token, recursing into response files when the token starts '@'.
inline void append_expanded_response_token(
    std::vector<ResponseToken>& output, ResponseToken token, int depth) {
    if (depth <= kMaxResponseDepth && token.value.starts_with('@')) {
        std::string_view path(token.value);
        path.remove_prefix(1);
        if (std::optional<std::string> text = read_response_file(path)) {
            append_response_file_tokens(output, path, *text, depth);
            return;
        }
    }

    output.push_back(std::move(token));
}

// Expands an argv array into owned tokens, preserving argv[0] as the program.
inline std::vector<ResponseToken> expand_response_files(
    int argc, const char* const argv[]) {
    std::vector<ResponseToken> output;
    if (argc <= 0 || argv == nullptr) {
        return output;
    }

    output.reserve(static_cast<size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        if (argv[index] == nullptr) {
            continue;
        }

        ResponseToken token{
            .value    = argv[index],
            .location = ValueLocation::argv(static_cast<size_t>(index)),
        };

        if (index == 0) {
            output.push_back(std::move(token));
        } else {
            append_expanded_response_token(output, std::move(token), 0);
        }
    }
    return output;
}

}  // namespace cmd::detail
