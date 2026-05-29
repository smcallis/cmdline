#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

#include "cmdline_error.h"
#include "cmdline_rules.h"

#if defined(__cpp_lib_constexpr_charconv) && \
    __cpp_lib_constexpr_charconv >= 202207L
#define CMDLINE_CONSTEXPR_CHARCONV constexpr
#else
#define CMDLINE_CONSTEXPR_CHARCONV
#endif

// Defines the contract for Types that convert from untyped spec strings to C++
// types, as well as built-in default types and the default type list. To add a
// new built-in type simply define it down below and add it to DefaultSpecTypes.

namespace cmd {

struct StringType;
struct IntType;
struct UintType;
struct FloatType;
struct DoubleType;

// The default type for untyped arguments and options.
using DefaultType = StringType;

// Defines the type contract for a Type for option and argument values. A Type
// is just a struct (or class) that exposes these three things:
//
//   - `type`  - a typedef indicating the C++ type T to use at runtime.
//   - `name`  - the name used to refer to the type in cmdline specs.
//   - `parse` - an (optionally constexpr) function that converts a string to T.
//
template <typename T>
concept SpecType = [] {
    using Error = Expected<typename T::type, TypeConversionError>;

    return requires(std::string_view text) {
        typename T::type;
        { T::name } -> std::convertible_to<std::string_view>;
        { T::parse(text) } -> std::same_as<Error>;
    };
}();

// Selects an entry from a parameter pack by index.
template <size_t index, typename... Entries>
using ElementAt = std::tuple_element_t<index, std::tuple<Entries...>>;

// A TypeList is a wrapper around a parameter pack that can find matching types.
template <SpecType... Types>
struct TypeList {
    static constexpr size_t size = sizeof...(Types);

    // Returns the last type index whose type name matches `name`.
    static constexpr size_t index(std::string_view name) {
        std::array<bool, sizeof...(Types)> matches = {
            (name == Types::name)...  //
        };

        // Iterate in reverse order to return the last matching type.
        for (size_t i = matches.size(); i > 0; --i) {
            if (matches[i - 1]) {
                return i - 1;
            }
        }
        return matches.size();
    }

    // Selects the matching type inside this list, or the default type.
    template <auto Name>
    using TypeByName = ElementAt<index(Name()), Types..., DefaultType>;
};

// Identifies a TypeList whose elements are all valid Types.
template <typename T>
concept SpecTypeList = requires {  //
    // Calling a function with a SpecType arg pack must work using T.
    []<SpecType... Types>(TypeList<Types...>) {}(T{});  //
};

// Returns the last type-list index that contains a type matching `name`.
template <SpecTypeList... Lists>
constexpr size_t type_list_index(std::string_view name) {
    std::array<bool, sizeof...(Lists)> matches = {
        (Lists::index(name) != Lists::size)...  //
    };

    // Iterate in reverse order to return the last matching type-list.
    for (size_t index = matches.size(); index > 0; --index) {
        if (matches[index - 1]) {
            return index - 1;
        }
    }
    return matches.size();
}

// Selects the matching type list inside a list of type lists.
template <auto Name, typename... Lists>
using ListByName =
    ElementAt<type_list_index<Lists...>(Name()), Lists..., TypeList<>>;

// Selects the value type matching `Name()`, or the default type.
template <auto Name, SpecTypeList... Lists>
using TypeByName =
    typename ListByName<Name, Lists...>::template TypeByName<Name>;

////////////////////////////////////////////////////////////////////////////////
// Built in types.
////////////////////////////////////////////////////////////////////////////////

// An unconstrained string value.
struct StringType {
    using type = std::string;

    static constexpr std::string_view name = kDefaultTypeName;

    static Expected<type, TypeConversionError> parse(std::string_view text) {
        return std::string(text);
    }
};

// A signed integer value parsed as an int64_t.
struct IntType {
    using type = int64_t;

    static constexpr std::string_view name = "int";

    // Parses a signed integer from a whole string.
    static CMDLINE_CONSTEXPR_CHARCONV Expected<type, TypeConversionError> parse(
        std::string_view text) {
        type value{};
        auto result =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} ||
            result.ptr != text.data() + text.size()) {
            return Unexpected(TypeConversionError("invalid int"));
        }
        return value;
    }
};

// An unsigned integer value parsed as a uint64_t.
struct UintType {
    using type = uint64_t;

    static constexpr std::string_view name = "uint";

    // Parses an unsigned integer from a whole string.
    static CMDLINE_CONSTEXPR_CHARCONV Expected<type, TypeConversionError> parse(
        std::string_view text) {
        type value{};
        auto result =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} ||
            result.ptr != text.data() + text.size()) {
            return Unexpected(TypeConversionError("invalid uint"));
        }
        return value;
    }
};

// A single-precision floating point value parsed from a whole string.
struct FloatType {
    using type = float;

    static constexpr std::string_view name = "float";

    // Parses a float from a whole string.
    static Expected<type, TypeConversionError> parse(std::string_view text) {
        type value{};
        auto result =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} ||
            result.ptr != text.data() + text.size()) {
            return Unexpected(TypeConversionError("invalid float"));
        }
        return value;
    }
};

// A double-precision floating point value parsed from a whole string.
struct DoubleType {
    using type = double;

    static constexpr std::string_view name = "double";

    // Parses a double from a whole string.
    static Expected<type, TypeConversionError> parse(std::string_view text) {
        type value{};
        auto result =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} ||
            result.ptr != text.data() + text.size()) {
            return Unexpected(TypeConversionError("invalid double"));
        }
        return value;
    }
};

////////////////////////////////////////////////////////////////////////////////

// List of default built-in types.
using DefaultSpecTypes =
    TypeList<StringType, IntType, UintType, FloatType, DoubleType>;

}  // namespace cmd

#undef CMDLINE_CONSTEXPR_CHARCONV
