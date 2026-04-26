#pragma once

#include <cstddef>
#include <string_view>

#include "cmdline_utils.h"

// Defines parser/runtime-neutral command-spec vocabulary and token predicates
// shared across parsing, spec checking, runtime parsing, and help generation.
//
// This file is limited to source line types, value types, repeat policies,
// built-in spellings, fallback names, and argv token classification.
//
// Syntax parsing, source-located validation, and runtime state/formatting live
// in other layers.

namespace cmd {

// Classifies each source line so help output can preserve spec layout.
enum class LineType {
    kBlank = 0,
    kRawText,
    kHeading,
    kArg,
    kOption,
};

// Classifies the value grammar accepted by an option.
enum class ValueType {
    kNone = 0,
    kString,
    kType,
    kChoice,
};

// Defines how repeated occurrences of the same option are handled.
enum class RepeatPolicy {
    kError = 0,
    kAll,
    kFirst,
    kLast,
};

// Defines the built-in help spellings and generated help text.
inline constexpr std::string_view kHelpLongName   = "help";
inline constexpr std::string_view kHelpShortName  = "h";
inline constexpr std::string_view kHelpLongToken  = "--help";
inline constexpr std::string_view kHelpShortToken = "-h";
inline constexpr std::string_view kHelpDescription =
    "Print this message and exit.";

// Defines the built-in fallback type name for untyped values.
inline constexpr std::string_view kDefaultTypeName = "string";

// Returns true when a long option name is reserved by the runtime parser.
inline constexpr bool is_reserved_long(std::string_view name) {
    return name == kHelpLongName;
}

// Returns true when a token matches one of the built-in help spellings.
inline constexpr bool is_help_token(std::string_view token) {
    return token == kHelpLongToken || token == kHelpShortToken;
}

// Returns true when one character is the built-in short help name.
inline constexpr bool is_help_short_name(char name) {
    return kHelpShortName.size() == 1 && name == kHelpShortName.front();
}

// Returns true when every present option name is a built-in help name.
inline constexpr bool uses_only_help_names(
    std::string_view long_name, std::string_view short_name) {
    bool has_any_name = !long_name.empty() || !short_name.empty();
    bool long_ok      = long_name.empty() || long_name == kHelpLongName;
    bool short_ok     = short_name.empty() || short_name == kHelpShortName;
    return has_any_name && long_ok && short_ok;
}

// Returns true when a token like '-vh' or '-hv' should trigger the built-in
// help escape hatch. This is only a boolean pre-scan, not token validation.
// Tokens containing '=' are rejected from this pre-scan and left for normal
// parsing so '-h=value' reports "help does not take a value" and '-vh=value'
// reports an invalid option block.
inline constexpr bool short_block_has_help(std::string_view token) {
    if (token.size() <= 2 || token.front() != '-' || token[1] == '-') {
        return false;
    }

    if (token.find('=') != std::string_view::npos) {
        return false;
    }

    for (size_t pos = 1; pos < token.size(); ++pos) {
        if (is_help_short_name(token[pos])) {
            return true;
        }
    }
    return false;
}

// Returns true when an argv token should be quoted in diagnostics.
inline constexpr bool needs_cmdline_quote(std::string_view token) {
    if (token.empty()) {
        return true;
    }

    for (char ch : token) {
        if (ch == '\'' || is_horizontal_space(ch)) {
            return true;
        }
    }
    return false;
}

}  // namespace cmd
