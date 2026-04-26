#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "cmdline_check.h"
#include "cmdline_error.h"
#include "cmdline_parse.h"
#include "cmdline_rules.h"
#include "cmdline_types.h"

// Defines the compile-time typing machinery. This provides TypedSpec which is a
// level above ParsedSpec from cmdline_parse.h after resolving argument/option
// types, checking optional compile-time default value parsing and merging it
// all together.
//
// TypedSpec is used while parsing argv to generate Cmdline (cmdline_match.h)
// which is the type returned to the user containing the parsed and converted
// command line values.

namespace cmd {
namespace detail {

// Sentinel returned by compile-time metadata lookups when no item matches.
inline constexpr size_t kNotFound = static_cast<size_t>(-1);

// Detects whether a callable can be invoked in a constant expression.
template <auto F, auto... Args>
concept ConstexprInvocable = requires {  //
    // The trick here is to use the comma operator to return true always. The
    // evaluation of F as a template arg won't succeed unless it's constexpr but
    // it doesn't have to return a valid NTTP value to use it here.
    []<bool = (F(Args...), true)>() {}();
};

// Takes one default value range from `Spec::raw.source`, unescapes quoted
// default syntax, and returns whether the selected Type parser accepts the
// resulting text in a constant expression.
template <SpecType Type, size_t kCapacity>
constexpr bool value_parses(
    std::string_view source, TextRange value, bool unescape) {
    std::array<char, kCapacity> text{};
    size_t count = 0;
    for (size_t i = 0; i < value.size; ++i) {
        char ch = source[value.begin + i];
        if (unescape && ch == '\\' && i + 1 < value.size &&
            (source[value.begin + i + 1] == '"' ||
             source[value.begin + i + 1] == '\\')) {
            text[count++] = source[value.begin + i + 1];
            ++i;
            continue;
        }

        text[count++] = ch;
    }
    return Type::parse(std::string_view(text.data(), count)).has_value();
}

// Scans default value ranges and returns the first one rejected by Type at
// compile time. An absent range means every value parsed successfully or
// compile-time parsing was not available for the type.
template <SpecType Type, typename Spec, auto kValues, bool kUnescape>
consteval TextRange invalid_typed_default() {
    TextRange invalid;
    auto scan = [&]<size_t... kIndexes>(std::index_sequence<kIndexes...>) {
        auto check = [&]<size_t kIndex> {
            if (!invalid) {
                constexpr auto parses = [] {
                    return value_parses<Type, Spec::raw.source.size()>(
                        Spec::raw.source, kValues.values[kIndex], kUnescape);
                };

                if constexpr (ConstexprInvocable<parses>) {
                    if constexpr (!parses()) {
                        invalid = kValues.values[kIndex];
                    }
                }
            }
        };

        (check.template operator()<kIndexes>(), ...);
    };

    scan(std::make_index_sequence<kValues.count>{});
    return invalid;
}

}  // namespace detail

// Builds an empty parsed spec for the unreachable branch after fail.
template <
    FixedString kSpecText,
    size_t N = count_lines(trim_source(kSpecText.view())),
    size_t M = trim_source(kSpecText.view()).size()>
consteval auto empty_parsed_spec() {
    constexpr std::string_view source = trim_source(kSpecText.view());
    ParsedSpec<N, M> spec{
        .source = StaticText<M>(source),
    };
    spec.valid = false;
    return spec;
}

// Parses and semantically checks a spec, failing compilation on error.
template <FixedString kSpecText>
consteval auto checked_spec() {
    constexpr auto parsed = parse_spec<kSpecText>();
    if constexpr (!parsed) {
        fail<kSpecText, parsed.error()>();
        return empty_parsed_spec<kSpecText>();
    } else {
        constexpr ParseStatus checked = check_spec(*parsed);
        if constexpr (!checked) {
            fail<kSpecText, checked.error()>();
            return empty_parsed_spec<kSpecText>();
        } else {
            return *parsed;
        }
    }
}

// Holds the compile-time form of a parsed and checked spec after value type
// names have been resolved. This is the layer used by typed accessors and
// compile-time default validation.
template <auto kParsed, SpecTypeList... Lists>
struct TypedSpec {
    // The syntax-level parsed spec this type layer adapts.
    static constexpr auto raw     = kParsed;
    static constexpr size_t nline = raw.nline;
    static constexpr size_t narg  = raw.narg;
    static constexpr size_t nopt  = raw.nopt;

    // Returns a parsed positional argument spec by index.
    static constexpr ArgSpec arg_spec(size_t index) { return raw.args[index]; }

    // Returns a parsed option spec by index.
    static constexpr OptSpec opt_spec(size_t index) { return raw.opts[index]; }

    // Finds the positional argument matching an accessor name.
    static constexpr size_t find_arg_index(std::string_view name) {
        for (size_t i = 0; i < narg; ++i) {
            if (same_text(raw, arg_spec(i).name, name)) {
                return i;
            }
        }

        return detail::kNotFound;
    }

    // Finds the option matching a --long or -s accessor name.
    static constexpr size_t find_opt_index(std::string_view name) {
        if (name.starts_with("--")) {
            name.remove_prefix(2);
            for (size_t i = 0; i < nopt; ++i) {
                if (same_text(raw, opt_spec(i).long_name, name)) {
                    return i;
                }
            }
        } else if (name.starts_with("-")) {
            name.remove_prefix(1);
            for (size_t i = 0; i < nopt; ++i) {
                if (same_text(raw, opt_spec(i).short_name, name)) {
                    return i;
                }
            }
        }

        return detail::kNotFound;
    }

    // Returns the positional argument index for an accessor name.
    template <FixedString kName>
    static constexpr size_t arg_index() {
        constexpr size_t index = find_arg_index(kName.view());
        static_assert(index != detail::kNotFound, "unknown argument");
        return index;
    }

    // Returns the option index for a --long or -s accessor name.
    template <FixedString kName>
    static constexpr size_t opt_index() {
        constexpr size_t index = find_opt_index(kName.view());
        static_assert(index != detail::kNotFound, "unknown option");
        return index;
    }

    // Returns whether an accessor name targets the built-in help switch.
    static constexpr bool is_help_name(std::string_view name) {
        return is_help_token(name);
    }

    // Provides the effective type lookup name for a positional argument.
    template <size_t kIndex>
    static constexpr std::string_view arg_type_name() {
        return raw.arg_type_name(kIndex);
    }

    // Provides the effective type lookup name for an option.
    template <size_t kIndex>
    static constexpr std::string_view opt_type_name() {
        return raw.opt_type_name(kIndex);
    }

    // Selects the resolved type for a positional argument.
    template <size_t kIndex>
    using ArgType =
        cmd::TypeByName<arg_type_name<kIndex>, DefaultSpecTypes, Lists...>;

    // Selects the resolved type for an option.
    template <size_t kIndex>
    using OptType =
        cmd::TypeByName<opt_type_name<kIndex>, DefaultSpecTypes, Lists...>;

    // Selects the C++ value type for a positional argument.
    template <size_t kIndex>
    using ArgValueType = typename ArgType<kIndex>::type;

    // Selects the C++ value type for an option.
    template <size_t kIndex>
    using OptValueType = typename OptType<kIndex>::type;

    // Validates one option default after type lookup is available.
    template <size_t kIndex>
    static consteval void validate_opt_default() {
        using Type = OptType<kIndex>;

        constexpr OptSpec opt = opt_spec(kIndex);
        if constexpr (opt.default_value && opt.value_type == ValueType::kType) {
            constexpr bool split_defaults = opt.keeps_all();

            constexpr auto default_values =
                maybe_split<opt.default_value.size + 1>(
                    raw.source, opt.default_value, split_defaults);

            constexpr TextRange invalid = detail::invalid_typed_default<
                Type, TypedSpec, default_values, opt.default_quoted>();

            if constexpr (invalid) {
                constexpr SourceLocation location =
                    location_for(raw.source, invalid.begin);

                fail_parsed<
                    raw,
                    ParseError{location, ParseError::kInvalidDefaultValue}>();
            }
        }
    }

    // Validates all option defaults that need resolved type metadata.
    template <size_t... Index>
    static consteval bool validate_defaults(std::index_sequence<Index...>) {
        (validate_opt_default<Index>(), ...);
        return true;
    }

    static constexpr bool defaults_valid =
        validate_defaults(std::make_index_sequence<nopt>{});

    // Represents a positional argument as either one value or a variadic
    // vector.
    template <size_t kIndex>
    using ArgData = std::conditional_t<
        arg_spec(kIndex).variadic,          //
        std::vector<ArgValueType<kIndex>>,  //
        ArgValueType<kIndex>                //
        >;

    // Represents a valueless option as either a flag or occurrence count.
    template <size_t kIndex>
    using SwitchData = std::conditional_t<
        opt_spec(kIndex).counting,  //
        int,                        //
        bool                        //
        >;

    // Represents a single value-taking option.
    template <size_t kIndex>
    using ScalarOptData = std::conditional_t<
        opt_spec(kIndex).is_optional_value(),  //
        std::optional<OptValueType<kIndex>>,   //
        OptValueType<kIndex>                   //
        >;

    // Represents a value-taking option according to its repeat policy.
    template <size_t kIndex>
    using ValueOptData = std::conditional_t<
        opt_spec(kIndex).keeps_all(),       //
        std::vector<OptValueType<kIndex>>,  //
        ScalarOptData<kIndex>               //
        >;

    // Represents any option according to value and repeat semantics.
    template <size_t kIndex>
    using OptData = std::conditional_t<
        opt_spec(kIndex).takes_value,  //
        ValueOptData<kIndex>,          //
        SwitchData<kIndex>             //
        >;

    // Allows staged tuple construction without default-constructing values.
    template <typename Value>
    using DataSlot = std::optional<Value>;

    // Builds the positional argument data tuple type.
    template <size_t... Index>
    static auto arg_data_tuple(std::index_sequence<Index...>)
        -> std::tuple<DataSlot<ArgData<Index>>...>;

    // Builds the option data tuple type.
    template <size_t... Index>
    static auto opt_data_tuple(std::index_sequence<Index...>)
        -> std::tuple<DataSlot<OptData<Index>>...>;

    // Data tuple used by Cmdline after parsing positional arguments.
    using ArgDataTuple =
        decltype(arg_data_tuple(std::make_index_sequence<narg>{}));

    // Data tuple used by Cmdline after parsing options.
    using OptDataTuple =
        decltype(opt_data_tuple(std::make_index_sequence<nopt>{}));

    // Selects the result type for a positional argument accessor.
    template <FixedString kName>
    using ArgResult = ArgData<arg_index<kName>()>;

    // Selects the result type for an option accessor.
    template <FixedString kName>
    static constexpr auto opt_result_type() {
        if constexpr (is_help_name(kName.view())) {
            return std::type_identity<bool>{};
        } else {
            return std::type_identity<OptData<opt_index<kName>()>>{};
        }
    }

    // Selects the result type for an option accessor or built-in help.
    template <FixedString kName>
    using OptResult = typename decltype(opt_result_type<kName>())::type;
};

}  // namespace cmd
