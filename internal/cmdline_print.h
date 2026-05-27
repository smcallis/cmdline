#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <unistd.h>

#include "cmdline_error.h"
#include "cmdline_faces.h"
#include "cmdline_model.h"
#include "cmdline_rules.h"

// Contains runtime usage metadata plus printable usage and diagnostic
// formatting. Compile-time syntax parsing and semantic checks live in
// cmdline_parse.h and cmdline_check.h.

namespace cmd {

// Returns whether generated output should include terminal styling.
inline bool color_enabled(FILE* file) {
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }

    if (std::getenv("FORCE_COLOR") != nullptr) {
        return true;
    }

    return file != nullptr && isatty(fileno(file)) != 0;
}

// Holds a printable command line and a caret column within it.
struct CmdlineContext {
    std::string line;
    size_t column = 0;
};

inline constexpr size_t kContextSize = 80;

namespace detail {

// Holds one argv token escaped for display and a mapped source offset.
struct QuotedArgvToken {
    std::string text;
    size_t offset = 0;
};

// Takes one argv token plus an offset in the original token and returns the
// printable token text plus the corresponding offset in that printed text.
// Tokens with spaces, tabs, quotes, or empty text are shell-quoted, and
// embedded single quotes are escaped.
inline QuotedArgvToken quote_cmdline_token(
    std::string_view token, size_t offset) {
    offset = std::min(offset, token.size());
    if (!needs_cmdline_quote(token)) {
        return {std::string(token), offset};
    }

    QuotedArgvToken result;
    result.text.push_back('\'');
    for (size_t i = 0; i < token.size(); ++i) {
        if (i == offset) {
            result.offset = result.text.size();
        }

        if (token[i] == '\'') {
            result.text += "'\\''";
        } else {
            result.text.push_back(token[i]);
        }
    }

    if (offset == token.size()) {
        result.offset = result.text.size();
    }
    result.text.push_back('\'');
    return result;
}

}  // namespace detail

// Takes a reconstructed command line and caret column, then trims it to a
// bounded window around the caret. Ellipses are inserted when text is removed
// from either side, and the caret column is remapped into the trimmed string.
inline CmdlineContext trim_cmdline_context(CmdlineContext context) {
    if (context.line.size() <= kContextSize) {
        return context;
    }

    size_t original_size = context.line.size();
    size_t begin         = 0;
    if (context.column > kContextSize / 2) {
        begin = context.column - (kContextSize / 2);
    }

    if (begin + kContextSize > context.line.size()) {
        begin = context.line.size() - kContextSize;
    }

    context.column -= begin;
    context.line = context.line.substr(begin, kContextSize);
    if (begin != 0) {
        context.line.replace(0, 3, "...");
    }

    if (begin + kContextSize < original_size) {
        context.line.replace(context.line.size() - 3, 3, "...");
    }

    if (!context.line.empty() && context.column >= context.line.size()) {
        context.column = context.line.size() - 1;
    }
    return context;
}

// Takes an error plus the original argv array and builds the single context
// line used by runtime diagnostics. Source-line errors, such as environment and
// default value warnings, bypass argv reconstruction and use their stored line
// and column directly.
inline CmdlineContext make_cmdline_context(
    const CmdlineError& error, int argc, const char* const argv[]) {
    const ValueLocation& location = error.location;
    if (!location.source_line.empty()) {
        return trim_cmdline_context(
            CmdlineContext{location.source_line, location.source_column});
    }

    CmdlineContext context;
    for (int index = 0; index < argc; ++index) {
        if (index != 0) {
            context.line.push_back(' ');
        }

        detail::QuotedArgvToken token;
        if (argv[index] != nullptr) {
            token = detail::quote_cmdline_token(
                argv[index], location.argument_offset);
        }

        if (location.has_location &&
            static_cast<size_t>(index) == location.argument_index) {
            context.column = context.line.size() + token.offset;
        }

        context.line += token.text;
    }

    if (!location.has_location ||
        location.argument_index >= static_cast<size_t>(argc)) {
        context.column = context.line.size();
    }

    if (context.column > context.line.size()) {
        context.column = context.line.size();
    }
    return trim_cmdline_context(std::move(context));
}

// Joins message fragments with a separator for compact diagnostics.
inline std::string join_message_values(
    const std::vector<std::string>& values, std::string_view separator) {
    std::string text;
    for (const std::string& value : values) {
        if (!text.empty()) {
            text += separator;
        }
        text += value;
    }
    return text;
}

// Formats only the message portion of a structured runtime parse error.
inline std::string cmdline_error_message(const CmdlineError& error) {
    const CmdlineErrorInfo& info = error.info;
    switch (info.type) {
        case CmdlineErrorType::kBadChoice:
            return fmt::format(
                "'{}' is not an allowed value for {} (valid: {})", info.value,
                info.subject, join_message_values(info.choices, "|"));

        case CmdlineErrorType::kInvalidValue: {
            std::string message = fmt::format(
                "'{}' is not a valid {} for {}", info.value, info.type_name,
                info.subject);
            if (!info.reason.empty()) {
                message += fmt::format(": {}", info.reason);
            }
            return message;
        }

        case CmdlineErrorType::kUnexpectedArgument:
            return fmt::format("unexpected argument: {}", info.value);

        case CmdlineErrorType::kUnexpectedPositional:
            return fmt::format(
                "unexpected positional argument: {}", info.value);

        case CmdlineErrorType::kUnknownOption:
            return fmt::format("unknown option: {}", info.subject);

        case CmdlineErrorType::kNoValueAllowed:
            return fmt::format("{} does not take a value", info.subject);

        case CmdlineErrorType::kValueRequired:
            return fmt::format("{} requires a value", info.subject);

        case CmdlineErrorType::kRepeatedOption:
            return fmt::format("{} was specified more than once", info.subject);

        case CmdlineErrorType::kMissingArgument:
            return fmt::format("missing positional argument: {}", info.subject);

        case CmdlineErrorType::kMissingOptionValue:
            return fmt::format("missing option value: {}", info.subject);

        case CmdlineErrorType::kNone: return "command-line parsing failed";
    }
    return "command-line parsing failed";
}

// Appends the same floating gutter prefix used by generated usage text.
inline void append_diagnostic_prefix(
    std::string& output, const FaceMap& faces, bool color) {
    output += " ";
    output += styled(find_face(faces, "cmdline-rail"), "│", color);
    output += "  ";
}

// Appends the source context and caret line used by runtime diagnostics.
inline void append_cmdline_context(
    std::string& output, const CmdlineContext& context, const FaceMap& faces,
    std::string_view context_face, std::string_view caret_face, bool color) {
    if (context.line.empty()) {
        return;
    }

    append_diagnostic_prefix(output, faces, color);
    output += styled(find_face(faces, context_face), context.line, color);
    output += "\n";

    append_diagnostic_prefix(output, faces, color);
    output.append(context.column, ' ');
    output += styled(find_face(faces, caret_face), "^-- ", color);
}

// Formats a runtime parse error, including one blank separator line, a
// color-aware label, command-line context, a caret pointing at the error
// location, and the final parser message.
inline std::string format_cmdline_error(
    const CmdlineError& error, int argc, const char* const argv[],
    const FaceMap& faces, bool color) {
    std::string output;
    output += "\n";
    append_diagnostic_prefix(output, faces, color);
    output += styled(find_face(faces, "cmdline-error-label"), "error:", color);
    output += " command-line parsing failed\n";

    CmdlineContext context = make_cmdline_context(error, argc, argv);
    append_cmdline_context(
        output, context, faces, "cmdline-error-context", "cmdline-error-caret",
        color);

    output += styled(
        find_face(faces, "cmdline-error-message"), cmdline_error_message(error),
        color);
    output += "\n";
    return output;
}

// Formats a non-fatal runtime warning using the same context and caret layout
// as errors. This is currently used for invalid environment fallbacks that are
// ignored instead of aborting the parse.
inline std::string format_cmdline_warning(
    const CmdlineError& warning, const FaceMap& faces, bool color) {
    std::string output;
    output += "\n";
    append_diagnostic_prefix(output, faces, color);
    output +=
        styled(find_face(faces, "cmdline-warning-label"), "warning:", color);
    output += " ignoring invalid environment variable\n";

    CmdlineContext context = make_cmdline_context(warning, 0, nullptr);
    append_cmdline_context(
        output, context, faces, "cmdline-warning-context",
        "cmdline-warning-caret", color);

    output += styled(
        find_face(faces, "cmdline-warning-message"),
        cmdline_error_message(warning), color);
    output += "\n";
    return output;
}

// Appends spaces until `current` reaches `target`.
inline void append_spaces(std::string& output, size_t current, size_t target) {
    if (current < target) {
        output.append(target - current, ' ');
    }
}

// Returns the column where positional descriptions should start.
inline size_t positional_description_column(
    const std::vector<SpecLine>& lines) {
    size_t column = 0;
    for (const SpecLine& line : lines) {
        if (line.type == LineType::kArg) {
            column = std::max(column, 2 + line.pos_arg.usage_width() + 2);
        }
    }
    return column;
}

// Returns the column where option descriptions should start.
inline size_t option_description_column(const std::vector<SpecLine>& lines) {
    size_t column = 0;
    for (const SpecLine& line : lines) {
        if (line.type == LineType::kOption && !line.option.hidden) {
            column = std::max(column, line.option.usage_width() + 3);
        }
    }
    return column;
}

// Returns the column where option attribute blocks should start.
inline size_t option_attribute_column(
    const std::vector<SpecLine>& lines, size_t desc_col) {
    size_t column = 0;
    for (const SpecLine& line : lines) {
        if (line.type == LineType::kOption && !line.option.hidden &&
            !line.option.default_value.empty()) {
            column = std::max(
                column, desc_col + 3 + line.option.description.size() + 2);
        }
    }
    return column;
}

// Stores the precomputed alignment columns for usage text.
struct UsageColumns {
    size_t arg{};   // Column where positional descriptions begin.
    size_t opt{};   // Column where option descriptions begin.
    size_t attr{};  // Column where option attribute blocks begin.
};

// Writes formatted usage text using shared render state.
struct UsageWriter {
    std::string& output;
    const FaceMap& faces;
    UsageColumns columns;
    bool color{};

    // Applies `face` to `text` when this writer emits styled output.
    std::string style(fmt::text_style face, std::string_view text) const {
        return styled(face, text, color);
    }

    // Looks up and applies a named usage face.
    std::string style(std::string_view name, std::string_view text) const {
        return style(find_face(faces, name), text);
    }

    // Returns the formatted built-in help switch token.
    std::string help_token() const {
        std::string token;
        token += style("cmdline-attr-bracket", "[");
        token += style("cmdline-help", kHelpLongToken);
        token += style("cmdline-opt-separator", "/");
        token += style("cmdline-help", kHelpShortToken);
        token += style("cmdline-attr-bracket", "]");
        return token;
    }

    // Returns the argument spelling used in generated usage text.
    std::string arg_usage_token(const PosArg& arg) const {
        std::string token;
        token += style("cmdline-arg-bracket", "<");
        token += style(arg.face_name, arg.usage_label());
        token += style("cmdline-arg-bracket", ">");

        char marker = arg.marker();
        if (marker != '\0') {
            token += style("cmdline-arg-repeat", std::string(1, marker));
        }
        return token;
    }

    // Returns the value placeholder used in usage and help text.
    std::string option_value_label(const Option& option) const {
        if (!option.takes_value) {
            return "";
        }

        if (!option.choices.empty()) {
            std::string label;
            label += style("cmdline-opt-choice-bracket", "[");
            label += join_choices(option);
            label += style("cmdline-opt-choice-bracket", "]");
            return label;
        }

        if (option.value_label_name.empty()) {
            return "";
        }

        std::string label;
        label += style("cmdline-opt-value-bracket", "<");
        label += style(option.value_face_name, option.value_label_name);
        label += style("cmdline-opt-value-bracket", ">");
        return label;
    }

    // Returns the long option spelling used in generated usage text.
    std::string option_long_usage_form(const Option& option) const {
        if (option.has_negative_form()) {
            std::string token;
            token += style("cmdline-opt-name", "--");
            token += style("cmdline-opt-toggle", "[no-]");
            token += style("cmdline-opt-name", option.long_name);
            return token;
        }
        return style("cmdline-opt-name", option.long_form());
    }

    // Returns the option spelling used in generated usage text.
    std::string option_usage_token(const Option& option) const {
        std::string token;
        if (!option.long_name.empty()) {
            token = option_long_usage_form(option);
            if (!option.short_name.empty()) {
                token += style("cmdline-opt-separator", "/");
                token += style("cmdline-opt-name", "-" + option.short_name);
            }
        } else {
            token = style("cmdline-opt-name", "-" + option.short_name);
        }

        if (option.takes_value) {
            token += style("cmdline-opt-value-separator", "=");
            token += option_value_label(option);
        }
        return token;
    }

    // Returns the repeat marker used in generated usage text.
    std::string option_repeat_token(const Option& option) const {
        switch (option.repeat) {
            case RepeatPolicy::kAll:   return style("cmdline-opt-repeat", "+");
            case RepeatPolicy::kFirst: return style("cmdline-opt-repeat", "<");
            case RepeatPolicy::kLast:  return style("cmdline-opt-repeat", ">");
            case RepeatPolicy::kError: return "";
        }
        return "";
    }

    // Returns the pipe-separated choice list used in usage text.
    std::string join_choices(const Option& option) const {
        std::string text;
        for (size_t index = 0; index < option.choices.size(); ++index) {
            if (index != 0) {
                text += style("cmdline-opt-choice-separator", "|");
            }
            text += option.choices[index];
        }
        return text;
    }

    // Appends the global usage gutter.
    void append_line_prefix(size_t indent) {
        output += " ";
        output += style("cmdline-rail", "│") + "  ";
        output.append(indent, ' ');
    }

    // Appends one positional argument help line.
    void append_positional(const PosArg& arg, size_t indent) {
        std::string token = arg_usage_token(arg);
        append_line_prefix(indent);
        output += "  " + token;

        append_spaces(
            output, indent + 2 + arg.usage_width(), indent + columns.arg);
        output += arg.description + "\n";
    }

    // Appends one option or switch help line.
    void append_option(const Option& option, size_t indent) {
        std::string token = option_usage_token(option);
        append_line_prefix(indent);
        output += token;

        size_t column = indent + option.usage_width();
        append_spaces(output, column, indent + columns.opt - 1);

        std::string marker = option_repeat_token(option);
        if (marker.empty()) {
            output += " ";
        } else {
            output += marker;
        }

        output += style("cmdline-description-rail", "│") + " ";
        column = indent + columns.opt + 2 + option.description.size();
        output += option.description;

        if (!option.default_value.empty()) {
            append_spaces(output, column, indent + columns.attr);
            output += style("cmdline-attr-bracket", "[");
            output += style("cmdline-attr-default-label", "default: ");
            output += style("cmdline-attr-default-value", option.default_value);
            output += style("cmdline-attr-bracket", "]");
        }
        output += "\n";
    }

    // Returns a gutter-only blank line for the global usage rail.
    std::string blank_line() const {
        std::string line = " ";
        line += style("cmdline-rail", "│");
        line += "\n";
        return line;
    }

    // Appends a global gutter line with no trailing content.
    void append_blank_line() { output += blank_line(); }

    // Replaces the final gutter-only line with a plain blank line.
    void float_bottom_blank_line() {
        std::string line = blank_line();
        if (output.size() < line.size()) {
            return;
        }

        size_t offset = output.size() - line.size();
        if (output.compare(offset, line.size(), line) != 0) {
            return;
        }
        output.replace(offset, line.size(), "\n");
    }

    // Appends one preserved spec line in declaration order.
    void append_line(const SpecLine& line) {
        switch (line.type) {
            case LineType::kBlank:  //
                append_blank_line();
                break;

            case LineType::kHeading:
                append_line_prefix(line.indent);
                output += style("cmdline-heading", line.text) + "\n";
                break;

            case LineType::kRawText:
                append_line_prefix(line.indent);
                output += line.text + "\n";
                break;

            case LineType::kArg:
                append_positional(line.pos_arg, line.indent);
                break;

            case LineType::kOption:
                if (!line.option.hidden) {
                    append_option(line.option, line.indent);
                }
                break;
        }
    }
};

// Formats complete usage text for a parsed runtime spec.
inline std::string usage_string(
    std::string_view program, const std::vector<SpecLine>& lines,
    const std::vector<PosArg>& args, const FaceMap& faces, bool color = false) {

    std::string output;
    std::vector<SpecLine> usage_lines = lines_with_help(lines);

    size_t opt_col = option_description_column(usage_lines);
    UsageColumns columns{
        positional_description_column(usage_lines),
        opt_col,
        option_attribute_column(usage_lines, opt_col),
    };

    UsageWriter writer{output, faces, columns, color};

    output += "\n";
    writer.append_line_prefix(0);
    output += writer.style("cmdline-usage", "Usage");
    output += writer.style("cmdline-usage-separator", ":");
    output += fmt::format(" {} {}", program, writer.help_token());
    for (const PosArg& arg : args) {
        output += fmt::format(" {}", writer.arg_usage_token(arg));
    }
    output += "\n";
    writer.append_blank_line();

    for (const SpecLine& line : usage_lines) {
        writer.append_line(line);
    }

    writer.float_bottom_blank_line();
    return output;
}

// Prints complete usage text for a parsed runtime spec.
inline void print_usage(
    FILE* file, std::string_view program, const std::vector<SpecLine>& lines,
    const std::vector<PosArg>& args, const FaceMap& faces) {
    const bool color = color_enabled(file);
    fmt::print(file, "{}", usage_string(program, lines, args, faces, color));
}

namespace detail {

// Prints a final diagnostic and exits with the corresponding error status.
[[noreturn]] inline void exit_with_error(
    const CmdlineError& error, int argc, const char* const argv[],
    std::span<const UsageFace> faces) {
    FaceMap face_map = default_faces(faces);
    fmt::print(
        stderr, "{}",
        format_cmdline_error(
            error, argc, argv, face_map, color_enabled(stderr)));

    int code = static_cast<int>(error.code);
    std::exit(code == 0 ? EXIT_FAILURE : code);
}

}  // namespace detail

}  // namespace cmd
