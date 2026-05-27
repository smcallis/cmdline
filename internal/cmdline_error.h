#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmdline_utils.h"

// Defines compile-time command-spec parse diagnostics and error string
// formatting. C++26 allows compile-time strings in static_asserts allowing for
// much nicer user generated compile-time messages. Prior to that we have to
// live with forcing a parsing error and getting identifiers printed to the
// screen by the compiler. Suboptimal but them's the breaks.

#if __cplusplus > 202302L && defined(__cpp_static_assert) && \
    __cpp_static_assert >= 202306L
#define CMDLINE_HAS_STATIC_ASSERT_MESSAGE 1
#else
#define CMDLINE_HAS_STATIC_ASSERT_MESSAGE 0
#endif

namespace cmd {

// Carries the first parse or check error found in a command spec.
struct ParseError {
    // Classifies parser errors for diagnostics and message formatting.
    // NOTE: Pre-C++26 these are really the only diagnostic text we can print
    // at compile time, so they're more verbose than they might otherwise be.
    enum Type {
        kUnknown = 0,
        kDefaultRequiresValue,
        kMisquotedDefaultValue,
        kDuplicateArg,
        kDuplicateOpt,
        kDuplicateRepeatMarker,
        kDuplicateTag,
        kEmptyChoiceValue,
        kEmptyDefaultValue,
        kEmptyEnvName,
        kEnvRequiresValue,
        kExpectedArgumentName,
        kExpectedChoiceClose,
        kExpectedLess,
        kExpectedOption,
        kExpectedOptionDelimiter,
        kExpectedOptionName,
        kExpectedShortOption,
        kExpectedTypeName,
        kExpectedTypeOrChoices,
        kExpectedValueClose,
        kInvalidChoiceValue,
        kInvalidDefaultValue,
        kInvalidEnvName,
        kNameContainsConsecutiveDash,
        kNameEndsWithDash,
        kLongOptionNameTooShort,
        kRequiredRequiresValue,
        kReservedOptionName,
        kTextAfterOptionTag,
        kTooManyArguments,
        kTooManyLines,
        kTooManyOptions,
        kUnknownOptionTag,
        kOptionalArgNotLast,
        kUnsupportedAlias,
        kVariadicArgNotLast,
    };

    SourceLocation location = {};
    Type type               = kUnknown;

    constexpr ParseError() = default;

    // Builds a command-spec parser error at a source location.
    constexpr ParseError(SourceLocation source_location, Type error_type)
        : location(source_location), type(error_type) {}

    // Returns the user-facing message for one parser error type.
    static constexpr std::string_view error_message(Type type) {
        // clang-format off
        switch (type) {
          case kDefaultRequiresValue:    return "default tag requires a value-taking option";
          case kDuplicateArg:            return "duplicate argument name";
          case kDuplicateOpt:            return "duplicate option name";
          case kDuplicateRepeatMarker:   return "duplicate repeat marker";
          case kDuplicateTag:            return "duplicate option tag";
          case kEmptyChoiceValue:        return "choice value cannot be empty";
          case kEmptyDefaultValue:       return "default tag value cannot be empty";
          case kEmptyEnvName:            return "env tag value cannot be empty";
          case kEnvRequiresValue:        return "env tag requires a value-taking option";
          case kExpectedArgumentName:    return "expected argument name";
          case kExpectedChoiceClose:     return "expected ']'";
          case kExpectedLess:            return "expected '<'";
          case kExpectedOption:          return "expected option";
          case kExpectedOptionDelimiter: return "expected option delimiter";
          case kExpectedOptionName:      return "expected option name";
          case kExpectedShortOption:     return "expected short option";
          case kExpectedTypeName:        return "expected type name";
          case kExpectedTypeOrChoices:   return "expected '<type>' or '[choices]'";
          case kExpectedValueClose:      return "expected '>'";
          case kInvalidChoiceValue:      return "choice value must be an identifier";
          case kInvalidDefaultValue:     return "invalid default value";
          case kInvalidEnvName:          return "environment variable name must start with alpha or '_' and contain only alphanumeric characters or '_'";
          case kNameContainsConsecutiveDash: return "name cannot contain consecutive '-'";
          case kNameEndsWithDash:        return "name cannot end with '-'";
          case kLongOptionNameTooShort:  return "long option name must have at least two characters";
          case kMisquotedDefaultValue:   return "misquoted default value";
          case kRequiredRequiresValue:   return "required tag requires a value-taking option";
          case kReservedOptionName:      return "reserved option name";
          case kTextAfterOptionTag:      return "option tags must come after description";
          case kTooManyArguments:        return "too many arguments";
          case kTooManyLines:            return "too many lines";
          case kTooManyOptions:          return "too many options";
          case kUnknownOptionTag:        return "unknown option tag";
          case kOptionalArgNotLast:      return "optional argument must be last";
          case kUnsupportedAlias:        return "option aliases are not supported";
          case kVariadicArgNotLast:      return "variadic argument must be last";
          case kUnknown:
              return "unknown command-line spec parse error";
        }
        // clang-format on
        return "unknown command-line spec parse error";
    }

    // Returns the user-facing message for this parse error.
    constexpr std::string_view error_message() const {
        return error_message(type);
    }
};

// Carries a value parser failure message.
struct TypeConversionError {
    std::string_view message{};

    constexpr TypeConversionError() = default;

    // Builds a type conversion error with caller-provided message text.
    constexpr explicit TypeConversionError(std::string_view text)
        : message(text) {}

    // Returns the user-facing message for this conversion failure.
    constexpr std::string_view error_message() const { return message; }
};

// Identifies where an option or argument value came from.
enum class ValueOrigin {
    kUnknown = 0,
    kArgv,          // Originated in the argv string.
    kResponseFile,  // Originated in a response file.
    kEnvironment,   // Originated in an environment variable.
    kDefault,       // Originated from a default tag.
};

// Records value provenance and caret placement for runtime diagnostics. Values
// from argv point back into the original token stream. Response files,
// environment values, and default values carry source lines that can be printed
// directly.
struct ValueLocation {
    ValueOrigin origin     = ValueOrigin::kUnknown;
    bool has_location      = false;
    size_t argument_index  = 0;
    size_t argument_offset = 0;
    std::string source_name;
    size_t source_row = 0;
    std::string source_line;
    size_t source_column = 0;

    // Builds a source location for one argv value.
    static ValueLocation argv(size_t index, size_t offset = 0) {
        return {ValueOrigin::kArgv, true, index, offset};
    }

    // Builds a source location for one response file token.
    static ValueLocation response_file(
        std::string_view name, size_t row, std::string_view line,
        size_t column) {
        return {
            .origin        = ValueOrigin::kResponseFile,
            .source_name   = std::string(name),
            .source_row    = row,
            .source_line   = std::string(line),
            .source_column = column,
        };
    }

    // Returns this location with `offset` added to the caret column.
    ValueLocation with_offset(size_t offset) const {
        ValueLocation copy = *this;
        if (!copy.source_line.empty()) {
            copy.source_column += offset;
        } else {
            copy.argument_offset += offset;
        }
        return copy;
    }

    // Builds a source location for one environment value.
    static ValueLocation environment(
        std::string_view name, std::string_view value) {
        std::string line(name);
        line += "=";
        size_t column = line.size();
        line += value;
        return {
            .origin        = ValueOrigin::kEnvironment,
            .source_line   = std::move(line),
            .source_column = column,
        };
    }

    // Builds a source location for one defaulted value.
    static ValueLocation default_value(std::string_view value) {
        std::string line("[default: ");
        size_t column = line.size();
        line += value;
        line += "]";
        return {
            .origin        = ValueOrigin::kDefault,
            .source_line   = std::move(line),
            .source_column = column,
        };
    }
};

// Identifies the category of command-line parse failure.
enum class ErrorCode {
    kNothing     = -1,
    kUnknown     = 0,
    kOverflow    = 1,
    kUnderflow   = 2,
    kIncomplete  = 3,
    kMultiple    = 4,
    kBadArgument = 5,
    kInvalid     = 6,
    kAlreadySet  = 7,
};

// Classifies runtime command-line error details for final formatting.
enum class CmdlineErrorType {
    kNone = 0,
    kBadChoice,
    kInvalidValue,
    kUnexpectedArgument,
    kUnexpectedPositional,
    kUnknownOption,
    kNoValueAllowed,
    kValueRequired,
    kRepeatedOption,
    kMissingArgument,
    kMissingOptionValue,
};

// Stores structured runtime error details until the print layer formats them.
struct CmdlineErrorInfo {
    CmdlineErrorType type = CmdlineErrorType::kNone;
    std::string subject;
    std::string value;
    std::string type_name;
    std::string reason;
    std::vector<std::string> choices;
};

// Builds error details for failures centered on a subject like an option name.
inline CmdlineErrorInfo subject_info(
    CmdlineErrorType type, std::string_view subject) {
    return CmdlineErrorInfo{
        .type    = type,
        .subject = std::string(subject),
    };
}

// Builds error details for failures centered on a bad argv value.
inline CmdlineErrorInfo value_info(
    CmdlineErrorType type, std::string_view value) {
    return CmdlineErrorInfo{
        .type  = type,
        .value = std::string(value),
    };
}

// Builds error details for explicit choice-list validation failures.
inline CmdlineErrorInfo choice_info(
    std::string_view subject, std::string_view value,
    std::vector<std::string> choices) {
    return CmdlineErrorInfo{
        .type    = CmdlineErrorType::kBadChoice,
        .subject = std::string(subject),
        .value   = std::string(value),
        .choices = std::move(choices),
    };
}

// Builds error details for type conversion failures.
inline CmdlineErrorInfo typed_value_info(
    std::string_view name, std::string_view type_name, std::string_view value,
    std::string_view reason) {
    return CmdlineErrorInfo{
        .type      = CmdlineErrorType::kInvalidValue,
        .subject   = std::string(name),
        .value     = std::string(value),
        .type_name = std::string(type_name),
        .reason    = std::string(reason),
    };
}

// Describes a command-line parse or validation failure.
struct CmdlineError {
    ErrorCode code = ErrorCode::kNothing;
    CmdlineErrorInfo info;
    ValueLocation location;
};

// There's no great way to generate compile time error information in C++. The
// best we get is static_assert in C++26 which can use constexpr functions as
// the source of the error string. Prior to that feature we have to hack our way
// around it.
//
// For pre-C++26 we'll use a type called ErrorMessage which is an empty struct
// templated on a ParseError::Type. We'll instantiate it in a context that
// causes the error and prints the type information, giving some hint where the
// error was in the cmdline spec.

// Wraps the line, column, and ErrorMessage into a type.
template <size_t kLine, size_t kColumn, ParseError::Type Error>
struct SpecErrorAt {};

// Provides a dependent false value for delayed static assertions.
template <typename>
inline constexpr bool kAlwaysFalse = false;

#if CMDLINE_HAS_STATIC_ASSERT_MESSAGE
// Builds source context around the parse location for C++26 diagnostics.
template <
    auto kSourceText, size_t kLine, size_t kColumn, ParseError::Type kType>
consteval auto make_static_assert_message_from_source() {
    constexpr std::string_view source  = kSourceText.view();
    constexpr size_t kContextLineCount = 4;
    constexpr auto context =
        source_line_context(kLine, count_lines(source), kContextLineCount);

    StaticString<source.size() * 2 + 256> message{
        "\nInvalid command-line specification:\n"  //
    };

    message.append(
        "line ", kLine, ", column ", kColumn, ": ",
        ParseError::error_message(kType), "\n\n");

    for (size_t line = context.first; line <= context.last; ++line) {
        const size_t begin = line_begin(source, line);
        const size_t end   = line_end(source, line);

        message.append(source.substr(begin, end - begin), '\n');
        if (line == kLine) {
            for (size_t column = 1; column < kColumn; ++column) {
                message.append(' ');
            }
            message.append("^-- ", ParseError::error_message(kType), '\n');
        }
    }

    return message;
}

// Builds source context around a FixedString spec parse location.
template <auto kSpecText, size_t kLine, size_t kColumn, ParseError::Type kType>
consteval auto make_static_assert_message() {
    constexpr std::string_view source = trim_source(kSpecText.view());
    return make_static_assert_message_from_source<
        StaticText<source.size()>(source), kLine, kColumn, kType>();
}
#endif

#if CMDLINE_HAS_STATIC_ASSERT_MESSAGE
// Emits a source-context compile-time diagnostic for one parsed spec error.
template <auto kSpecText, size_t kLine, size_t kColumn, ParseError::Type kType>
consteval void fail() {
    using Diagnostic = SpecErrorAt<kLine, kColumn, kType>;
    static_assert(
        kAlwaysFalse<Diagnostic>,
        make_static_assert_message<kSpecText, kLine, kColumn, kType>());
}
#else
// Emits a line/column compile-time diagnostic for one parsed spec error.
template <auto, size_t kLine, size_t kColumn, ParseError::Type kType>
consteval void fail() {
    using Diagnostic = SpecErrorAt<kLine, kColumn, kType>;
    static_assert(
        kAlwaysFalse<Diagnostic>, "invalid command-line specification");
}
#endif

// Emits a compile-time diagnostic for one parsed spec error object.
template <auto kSpecText, ParseError kError>
consteval void fail() {
    fail<
        kSpecText, kError.location.line, kError.location.column, kError.type>();
}

#if CMDLINE_HAS_STATIC_ASSERT_MESSAGE
// Emits a source-context diagnostic for an error in an already parsed spec.
template <auto kParsed, size_t kLine, size_t kColumn, ParseError::Type kType>
consteval void fail_parsed_at() {
    using Diagnostic = SpecErrorAt<kLine, kColumn, kType>;
    static_assert(
        kAlwaysFalse<Diagnostic>, make_static_assert_message_from_source<
                                      kParsed.source, kLine, kColumn, kType>());
}
#else
// Emits a line/column diagnostic for an error in an already parsed spec.
template <auto, size_t kLine, size_t kColumn, ParseError::Type kType>
consteval void fail_parsed_at() {
    using Diagnostic = SpecErrorAt<kLine, kColumn, kType>;
    static_assert(
        kAlwaysFalse<Diagnostic>, "invalid command-line specification");
}
#endif

// Emits a compile-time diagnostic for one error after parsing has completed.
template <auto kParsed, ParseError kError>
consteval void fail_parsed() {
    fail_parsed_at<
        kParsed, kError.location.line, kError.location.column, kError.type>();
}

}  // namespace cmd

#undef CMDLINE_HAS_STATIC_ASSERT_MESSAGE
