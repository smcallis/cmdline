#pragma once

#include <cstddef>
#include <string_view>

#include "cmdline_parse.h"
#include "cmdline_rules.h"

// Handles semantic verification after parsing a spec into a ParsedSpec. Rules
// like "no duplicate option names" and "choice defaults must be valid" are
// checked in here. The main entry point is `check_spec`.

namespace cmd {

// Compares two parsed-spec ranges by the text they reference.
template <size_t N, size_t M>
constexpr bool same_text(
    const ParsedSpec<N, M>& spec, TextRange left, TextRange right) {
    return left.view(spec.source) == right.view(spec.source);
}

// Compares a parsed-spec range with literal or caller-provided text.
template <size_t N, size_t M>
constexpr bool same_text(
    const ParsedSpec<N, M>& spec, TextRange left, std::string_view right) {
    return left.view(spec.source) == right;
}

// Checks whether every present option name is a built-in help name.
template <size_t N, size_t M>
constexpr bool option_uses_only_help_names(
    const ParsedSpec<N, M>& spec, const OptSpec& option) {
    std::string_view long_name =
        option.long_name ? option.long_name.view(spec.source) : "";
    std::string_view short_name =
        option.short_name ? option.short_name.view(spec.source) : "";
    return uses_only_help_names(long_name, short_name);
}

// Checks that the default value for a choice option is valid.
template <size_t N, size_t M>
constexpr ParseStatus check_choice_default(
    const ParsedSpec<N, M>& spec, const OptSpec& option) {
    if (!option.default_value || option.value_type != ValueType::kChoice) {
        return {};
    }

    // Returns an OK status if the given value is a valid default value.
    const auto is_valid_default = [&](TextRange value) -> ParseStatus {
        if (!option.choice_allows_value(spec.source, value)) {
            return Unexpected(
                make_error(spec, value, ParseError::kInvalidDefaultValue));
        }
        return {};
    };

    if (option.repeat != RepeatPolicy::kAll) {
        return is_valid_default(option.default_value);
    }

    auto values = maybe_split<M + 1>(spec.source, option.default_value, true);
    for (size_t i = 0; i < values.count; ++i) {
        if (auto check = is_valid_default(values.values[i]); !check) {
            return check;
        }
    }
    return {};
}

// Checks whether variadic positional arguments appear only at the end.
template <size_t N, size_t M>
constexpr ParseStatus check_variadic_is_last(const ParsedSpec<N, M>& spec) {
    for (size_t i = 1; i < spec.narg; ++i) {
        if (spec.args[i - 1].variadic) {
            return Unexpected(make_error(
                spec, spec.args[i - 1].name, ParseError::kVariadicArgNotLast));
        }
    }
    return {};
}

// Checks whether optional positional arguments appear only at the end.
template <size_t N, size_t M>
constexpr ParseStatus check_optional_is_last(const ParsedSpec<N, M>& spec) {
    for (size_t i = 1; i < spec.narg; ++i) {
        if (spec.args[i - 1].optional()) {
            return Unexpected(make_error(
                spec, spec.args[i - 1].name, ParseError::kOptionalArgNotLast));
        }
    }
    return {};
}

// Checks for duplicate argument names.
template <size_t N, size_t M>
constexpr ParseStatus check_duplicate_arguments(const ParsedSpec<N, M>& spec) {
    for (size_t i = 0; i < spec.narg; ++i) {
        for (size_t j = i + 1; j < spec.narg; ++j) {
            if (same_text(spec, spec.args[i].name, spec.args[j].name)) {
                return Unexpected(make_error(
                    spec, spec.args[j].name, ParseError::kDuplicateArg));
            }
        }
    }
    return {};
}

// Checks that no accepted spelling for `left` duplicates one for `right`.
template <size_t N, size_t M>
constexpr ParseStatus check_duplicate_option_names(
    const ParsedSpec<N, M>& spec, const OptSpec& left, const OptSpec& right) {
    if (option_uses_only_help_names(spec, left) &&
        option_uses_only_help_names(spec, right)) {
        TextRange name = right.long_name ? right.long_name : right.short_name;
        return Unexpected(make_error(spec, name, ParseError::kDuplicateOpt));
    }

    if (left.long_name && right.long_name &&
        same_text(spec, left.long_name, right.long_name)) {
        return Unexpected(
            make_error(spec, right.long_name, ParseError::kDuplicateOpt));
    }

    if (left.short_name && right.short_name &&
        same_text(spec, left.short_name, right.short_name)) {
        return Unexpected(
            make_error(spec, right.short_name, ParseError::kDuplicateOpt));
    }

    if (left.long_name && right.long_name &&
        ((left.has_negative_form() &&
          left.negative_form_matches(spec.source, right.long_name)) ||
         (right.has_negative_form() &&
          right.negative_form_matches(spec.source, left.long_name)))) {
        return Unexpected(
            make_error(spec, right.long_name, ParseError::kDuplicateOpt));
    }
    return {};
}

// Checks that value-related tags are only attached to value options.
template <size_t N, size_t M>
constexpr ParseStatus check_value_tags(
    const ParsedSpec<N, M>& spec, const OptSpec& option) {
    if (option.takes_value) {
        return {};
    }

    if (option.default_value) {
        return Unexpected(make_error(
            spec, option.default_value, ParseError::kDefaultRequiresValue));
    }

    if (option.env_name) {
        return Unexpected(
            make_error(spec, option.env_name, ParseError::kEnvRequiresValue));
    }

    if (option.required) {
        TextRange name =
            option.long_name ? option.long_name : option.short_name;
        return Unexpected(
            make_error(spec, name, ParseError::kRequiredRequiresValue));
    }
    return {};
}

// Checks that reserved help names are only used as --help, -h, or --help/-h.
template <size_t N, size_t M>
constexpr ParseStatus check_reserved_help_names(
    const ParsedSpec<N, M>& spec, const OptSpec& option) {
    bool short_help =
        option.short_name && same_text(spec, option.short_name, kHelpShortName);
    if (option_uses_only_help_names(spec, option)) {
        if (option.takes_value || option.repeat != RepeatPolicy::kError ||
            option.default_value || option.env_name || option.required ||
            option.hidden) {
            TextRange name =
                option.long_name ? option.long_name : option.short_name;
            return Unexpected(
                make_error(spec, name, ParseError::kReservedOptionName));
        }
        return {};
    }

    if (short_help) {
        return Unexpected(make_error(
            spec, option.short_name, ParseError::kReservedOptionName));
    }

    if (option.long_name &&
        is_reserved_long(option.long_name.view(spec.source))) {
        return Unexpected(make_error(
            spec, option.long_name, ParseError::kReservedOptionName));
    }
    return {};
}

// Checks one option for reserved names, tag semantics, and choice defaults.
template <size_t N, size_t M>
constexpr ParseStatus check_option(
    const ParsedSpec<N, M>& spec, const OptSpec& option) {
    ParseStatus reserved = check_reserved_help_names(spec, option);
    if (!reserved) {
        return Unexpected(reserved.error());
    }

    ParseStatus value_tags = check_value_tags(spec, option);
    if (!value_tags) {
        return Unexpected(value_tags.error());
    }

    return check_choice_default(spec, option);
}

// Checks options for reserved names, invalid defaults, and duplicate names.
template <size_t N, size_t M>
constexpr ParseStatus check_options(const ParsedSpec<N, M>& spec) {
    for (size_t i = 0; i < spec.nopt; ++i) {
        const auto& left = spec.opts[i];

        ParseStatus status = check_option(spec, left);
        if (!status) {
            return Unexpected(status.error());
        }

        for (size_t j = i + 1; j < spec.nopt; ++j) {
            const auto& right = spec.opts[j];

            ParseStatus duplicate =
                check_duplicate_option_names(spec, left, right);
            if (!duplicate) {
                return Unexpected(duplicate.error());
            }
        }
    }
    return {};
}

// Checks semantic constraints that require the complete parsed spec.
template <size_t N, size_t M>
constexpr ParseStatus check_spec(const ParsedSpec<N, M>& spec) {
    ParseStatus variadic = check_variadic_is_last(spec);
    if (!variadic) {
        return Unexpected(variadic.error());
    }

    ParseStatus optional = check_optional_is_last(spec);
    if (!optional) {
        return Unexpected(optional.error());
    }

    ParseStatus args = check_duplicate_arguments(spec);
    if (!args) {
        return Unexpected(args.error());
    }

    return check_options(spec);
}

}  // namespace cmd
