#pragma once

#include <cstdio>
#include <cstdlib>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/cmdline_match.h"

namespace cmd {

// This is a library for parsing a help-like specification at compile time,
// which is then used to parse the command line at runtime to read positional
// arguments, options and switches.
//
// Example usage
// ‾‾‾‾‾‾‾‾‾‾‾‾‾
//   int main(int argc, const char* argv[]) {
//       auto cmdline = cmd::parse_cmdline<R"(
//         Sync files to a remote destination.
//
//         _Arguments
//           <input>   Source directory to scan
//           <output>  Remote target path
//           <paths>+  File or directory paths to transfer
//
//         _Options
//           --config/-c=<path>       Config file to load [env: SYNC_CONFIG]
//           --format/-f=[json|yaml]  Output format [default: json]
//           --profile=<name>         Deployment profile
//           --tag/-t=<name>+         Attach one or more tags
//           --token=<token>          API token [env: SYNC_TOKEN]
//           --dry-run                Validate without writing remote changes
//           --verbose/-v+            Emit extra logging
//           -q                       Suppress non-error output
//       )">(argc, argv);
//
//       const std::string& input  = cmdline.arg<"input">();
//       const std::string& output = cmdline.arg<"output">();
//       const std::vector<std::string>& paths = cmdline.arg<"paths">();
//       const std::vector<std::string>& tags = cmdline.opt<"--tag">();
//
//       bool dry_run = cmdline.opt<"--dry-run">();
//       int verbosity = cmdline.opt<"-v">();
//
//       return run_sync(input, paths, output, tags, dry_run, verbosity);
//   }
//
//   $ ./sync --dry-run -vv input output ./one ./two --tag nightly
//
// The spec is ordinary text with a small amount of markup in it to define the
// arguments and options for the program. Common leading whitespace is removed,
// and an initial leading blank line is ignored so that the C++ raw string can
// start on its own line. After that, blank lines and manual indentation are
// preserved in the generated help text.
//
// The spec is parsed and checked at compile-time to ensure correctness and
// accessors are generated for each argument and option with a type to match
// their configuration.
//
// Misspelled names, malformed options, duplicate names, invalid defaults and
// other statically detectable mistakes will fail to compile.
//
// Syntax
// ‾‾‾‾‾‾
//   Identifiers
//     Argument names, option names, and type names are ASCII identifiers. They
//     may contain alphanumeric characters and `-`, and may start with a number.
//     Names may not start with `-`, end with `-`, or contain consecutive `-`
//     characters. Long option names must have at least two characters.
//
//   Raw text
//     Lines whose first non-space character is not `_`, `<`, or `-` are copied
//     into the generated help output. Empty lines are preserved.
//
//   Headings
//     A line whose first non-space character is `_` is printed as a heading.
//     That marker is stripped before formatting and printing the heading
//     text. Headings are display-only and do not create sections or affect
//     sorting. Indentation is manual, so indent headings and any lines below
//     them directly in the spec.
//
//   Positional arguments
//     Positional arguments are values that the user must provide in-order on
//     the command line and have one of these forms:
//
//       `<name>`        Required string argument
//       `<name>?`       Final zero-or-one optional string argument
//       `<name>+`       Final one-or-more variadic string argument
//       `<name>*`       Final zero-or-more variadic string argument
//       `<name:type>`   Required typed argument
//       `<name:type>?`  Final zero-or-one optional typed argument
//       `<name:type>+`  Final one-or-more variadic typed argument
//       `<name:type>*`  Final zero-or-more variadic typed argument
//
//     `?`, `+`, and `*` arguments must be last. `?` accepts zero or one value,
//     `+` accepts one or more values, and `*` accepts zero or more values. If
//     an argument name matches a registered type name, `<path>` is shorthand
//     for `<path:path>`. To use that name as a string argument, write
//     `<path:string>`.
//
//   Options and switches
//     Options have this general form.
//
//       names[=value-spec][repeat] [description] [tags]
//
//     Names may be long, short, or both:
//
//       --long
//       -s
//       --long/-s
//
//     `=` after the names makes the option consume a value. Type and choice
//     grammars must immediately follow `=`. With no value grammar after `=`,
//     the value is an unconstrained string.
//
//       `--token=`                  Long string value
//       `-t=`                       Short string value
//       `--config/-c=<path>`        Typed value
//       `--format/-f=[json|yaml]`   Choice value
//       `--dry-run`                 Boolean switch
//
//     At runtime, value options accept both `--token value` and
//     `--token=value`. Short value options accept `-t value` and `-t=value`.
//
//     Choices are identifiers separated by `|`. They use the same syntax as
//     argument, option, and type names, and do not support escaping.
//
//   Repetition
//     `+`, `<`, and `>` choose what happens when the same option appears more
//     than once. At most one marker may be specified. Without a marker,
//     repeating an option is an error.
//
//       `--tag=<name>+`    Keep every value in a vector
//       `--verbose/-v+`    Count switch uses, so -vvv returns 3
//       `--profile=<id><`  Keep the first value
//       `--cache=<path>>`  Keep the last value
//       `--watch>`         Last switch wins and --no-watch disables it
//
//     `<` and `>` make long switches toggleable by adding an implicit
//     `--no-long` spelling. Short-only switches such as `-q<` and `-q>` are
//     first-only or last-only switches, but they do not get a negative form.
//     Command line values that are ultimately ignored due to `<` or `>` are not
//     type-checked.
//
//   Descriptions and tags
//     Description text is the trimmed text after the option grammar and before
//     any tags. Use `\[` or `\]` to include literal brackets in descriptions.
//     Tags must come after the end of the description, and are specified as
//     separate `[]` delimited blocks:
//
//       `[default: value]`  Default value
//       `[env: NAME]`       Environment variable fallback
//       `[required]`        Require argv, env, or default value
//       `[hidden]`          Hide from generated help
//
//     `default`, `env`, and `required` are valid only on value-taking options.
//     Empty default values and empty environment names are rejected.
//
//     Environment variable names follow POSIX style, starting with an ASCII
//     letter or `_`, then alphanumeric characters or `_`. `default` values
//     containing horizontal whitespace must be double-quoted. Those quotes are
//     not part of the stored default. Use `\"` to include a double quote in a
//     quoted default, or `\\` to include a backslash. `]` still ends the tag.
//
// Runtime interface
// ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
//   Most users will want to use `parse_cmdline<spec>(argc, argv)` as their
//   entry point. It parses the command line spec, matches it to the given
//   command line values, and returns an opaque type that can be used to query
//   argument and option values. If any error occurs, it prints a useful error
//   message with context indicating the source of the error and exits the
//   program with an error status.
//
//   If `-h`, `--help`, or a grouped short-switch block containing `h` is given
//   before `--` at runtime, the usage string is printed and the program is
//   exited with a success status.
//
//   The generated help output includes a built-in `--help/-h` line. Declare a
//   plain `--help`, `-h`, or `--help/-h` switch yourself to control where that
//   line appears and what description it uses. `-h` remains reserved as the
//   short name for any non-help option.
//
//   If exiting the program is undesirable, then `try_parse_cmdline` is an
//   alternative that returns `std::expected<Cmdline, CmdlineError>`.
//   `CmdlineError` stores structured details and a value location. Use
//   `cmdline_error_message(error)` for a short message, or
//   `format_cmdline_error(...)` for the full context diagnostic.
//
//   If help was requested, the result succeeds and `opt<"--help">()`,
//   `opt<"-h">()`, `set<"--help">()`, and `set<"-h">()` return true. Other
//   argument and option accessors are outside the help-only API contract. Thus
//   the appropriate way to use it is:
//
//     auto cmdline = try_parse_cmdline<spec>(argc, argv);
//     if (cmdline && cmdline->opt<"--help">()) {
//       cmdline->usage(stdout);
//       <no further use of cmdline>
//     }
//
//   This is done to prevent improperly formed arguments from inhibiting
//   printing of the usage string.
//
//   Positional arguments are accessed by bare name with `arg<"name">()`.
//   Options and switches are accessed by their command-line spelling with
//   `opt<"--long">()` or `opt<"-s">()`. `set<"--long">()` and `set<"-s">()`
//   return whether that option was present on argv or in a response file. If an
//   option has a long and short name either is acceptable to use, both variants
//   return the same value. Accessor names are checked at compile time.
//
//   Accessors return const references to stored values.
//     - Positional arguments return `T`.
//     - Optional positional arguments return `std::optional<T>`.
//     - Variadic arguments return `std::vector<T>`.
//     - Optional value options return `std::optional<T>`.
//     - Required or defaulted value options return `T`.
//     - Repeating value options return `std::vector<T>`.
//     - Switches return `bool` (defaulting to false).
//     - Counting switches return `int`.
//
//   Command-line values beat environment variables. Environment variables beat
//   defaults. Environment values are only available at runtime, so their type
//   validation happens at runtime too. Defaults for repeat-all options and
//   environment values for repeat-all options are split on commas. Commas
//   cannot be escaped for repeat-all fallback values.
//
//   Invalid environment values are warnings rather than errors. They are
//   ignored, and defaults are used when available. `parse_cmdline` prints those
//   warnings before returning, while `try_parse_cmdline` returns them through
//   `warnings()`.
//
//   Value-taking options consume the next token greedily, even if it begins
//   with `-` or is exactly `--`. Otherwise, `--` stops option parsing and all
//   remaining tokens become positional arguments. A bare `-` is a positional
//   argument, so input and output files can use it to mean stdin or stdout.
//   Unknown options are errors. Short switches may be grouped as `-abc`, but a
//   short switch block may not contain a value-taking option. Short value
//   options accept `-o=value`, but not `-ovalue`.
//
//   Response files follow GCC behavior. An argv token like `@args.rsp` is
//   replaced by whitespace-separated tokens read from that file. Single quotes,
//   double quotes, and backslash escapes can preserve whitespace or include
//   special characters. Response files may refer to other response files. If a
//   response file cannot be read, the original `@file` token is left unchanged.
//
//   Generated usage preserves declaration order. It is colorized when written
//   to a TTY. Set FORCE_COLOR to force color output. Set NO_COLOR to disable
//   color output, which always wins over FORCE_COLOR.
//
// Types
// ‾‾‾‾‾
//   A cmdline type is a small struct or class that names a value type and knows
//   how to parse text into the C++ value type returned by the accessor. The
//   built-in types are `string`, `int`, `uint`, `float`, and `double`.
//   Unknown type names fall back to `string`.
//
//   Custom types can define new names or override built-ins. If multiple types
//   use the same name, the later type wins, and later type lists win over
//   earlier type lists.
//
//   A type defines this contract.
//
//       struct IntegerType {
//           using type = int;
//           static constexpr std::string_view name = "integer";
//
//           static std::expected<type, TypeConversionError> parse(
//               std::string_view text) {
//               ...
//           }
//       };
//
//   `name` is the string used to refer to the type in the spec, such as
//   `<input:path>` or `--count=<integer>`. `type` is the C++ type returned by
//   `arg<"name">()` or `opt<"--option">()`. `parse` converts raw command-line
//   text, default text, or environment text into that C++ type. Returning
//   `std::unexpected(TypeConversionError{"..."})` reports a parse failure.
//
//   Types are passed through one or more `TypeList`s:
//
//       using MyTypes = cmd::TypeList<PathType, IntegerType>;
//       auto cmdline = cmd::parse_cmdline<kSpec, MyTypes>(argc, argv);
//
//   Default values are converted when argv is parsed at runtime. If `parse`
//   can be evaluated in a constant expression for a specific default value, the
//   default is also checked while compiling the spec. Invalid constexpr-checked
//   defaults fail spec compilation at the default location. Repeated defaults
//   are split on commas before validation, and quoted defaults have escaped
//   quotes and backslashes unescaped first. Environment variables are still
//   runtime-only and are never compile-time validated.
//
// Faces
// ‾‾‾‾‾
//   Usage faces are runtime-only style bindings. They color syntax, value
//   placeholders, type names, and semantic names. Syntax faces use the prefix
//   `cmdline-`, which is reserved for the implementation. Built-in type and
//   semantic faces include `string`, `input`, and `output`. User-provided faces
//   are merged last and override built-ins. A face can style a placeholder
//   even when there is no custom type with the same name.
//
//       cmd::UsageFace faces[] = {
//           {"input", fmt::fg(fmt::color::steel_blue)},
//           {"output", fmt::fg(fmt::color::salmon)},
//       };
//
//       auto cmdline = cmd::parse_cmdline<kSpec, MyTypes>(
//           argc, argv, std::span{faces});
//
// More Examples
// ‾‾‾‾‾‾‾‾‾‾‾‾‾
//   A final positional argument that consumes one or more remaining values.
//     `<files>+`
//
//   A final positional argument that consumes zero or more remaining values.
//     `<files>*`
//
//   A final positional argument that consumes zero or one value.
//     `<config>?`
//
//   A switch enabled with --long and disabled with --no-long, last one wins.
//     `--long>`
//
//   A switch that can be disabled, first instance wins.
//     `--long<`
//
//   A short-only switch that may repeat, first instance wins.
//     `-q<`
//
//   A short-only switch that may repeat, last instance wins.
//     `-q>`
//
//   A switch that counts occurrences.
//     `--verbose/-v+`
//
//   An option requiring a string value.
//     `--option=`
//
//   A string option that must be provided by argv, environment, or default.
//     `--token= [required]`
//
//   An option requiring a value of a custom type named `path`.
//     `--option=<path>`
//
//   An option taking multiple custom `path` values and returning all of them.
//     `--option=<path>+`
//
//   An option that takes the last specified custom `path`.
//     `--option=<path>>`
//
//   An option that takes the first specified custom `path`.
//     `--option=<path><`
//
//   An option that takes one of a fixed set of values.
//     `--option=[one|two|three]`
//
//   An option that takes one of a fixed set of values, last one wins.
//     `--option=[one|two|three]>`
//
//   An option with a default choice.
//     `--option=[one|two|three] [default: two]`
//
//   An option with a quoted default containing whitespace.
//     `--message= [default: "hello world"]`
//
//   An option with an environment fallback.
//     `--option=<path> [env: INPUT_PATH]`
//
//   An option with an environment fallback and default value.
//     `--config= [env: CONFIG_PATH] [default: config.json]`
//
//   An option hidden from generated usage text.
//     `--debug-dump= [hidden]`

namespace detail {

// Keeps user code syntactically valid after an invalid spec has already emitted
// its compile-time diagnostic. It prevents cascaded accessor errors.
struct InvalidCmdlineValue {
    operator const std::string&() const {
        static const std::string value;
        return value;
    }

    operator const std::vector<std::string>&() const {
        static const std::vector<std::string> value;
        return value;
    }

    operator bool() const { return false; }
    operator int() const { return 0; }

    const std::string& operator*() const {
        static const std::string value;
        return value;
    }

    const std::string* operator->() const {
        static const std::string value;
        return &value;
    }
};

// Dummy command-line result used only after spec compilation has failed.
struct InvalidCmdline {
    template <FixedString>
    const InvalidCmdlineValue& arg() const {
        static const InvalidCmdlineValue value;
        return value;
    }

    template <FixedString>
    const InvalidCmdlineValue& opt() const {
        static const InvalidCmdlineValue value;
        return value;
    }

    template <FixedString>
    bool set() const {
        return false;
    }

    bool help_requested() const { return false; }
    void usage(FILE* = stdout) const {}
    void print_warnings(FILE* = stderr) const {}
};

template <FixedString kSpec, SpecTypeList... Lists>
using RuntimeSpec = TypedSpec<checked_spec<kSpec>(), Lists...>;

}  // namespace detail

}  // namespace cmd

template <>
struct fmt::formatter<cmd::detail::InvalidCmdlineValue>
    : fmt::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(
        const cmd::detail::InvalidCmdlineValue&, FormatContext& context) const {
        return fmt::formatter<std::string_view>::format("", context);
    }
};

namespace cmd {

template <FixedString kSpec, SpecTypeList... Lists>
using Cmdline = detail::Cmdline<detail::RuntimeSpec<kSpec, Lists...>>;

// Parses argv using a compile-time validated spec and returns errors.
template <FixedString kSpec, SpecTypeList... Lists>
auto try_parse_cmdline(
    int argc, const char* const argv[], std::span<const UsageFace> faces = {}) {
    constexpr auto spec = checked_spec<kSpec>();
    if constexpr (!spec.valid) {
        return std::expected<detail::InvalidCmdline, CmdlineError>(
            detail::InvalidCmdline{});
    } else {
        using Spec = TypedSpec<spec, Lists...>;
        using Type = detail::Cmdline<Spec>;
        static_assert(Spec::defaults_valid);
        if (detail::argv_requests_help(argc, argv)) {
            return std::expected<Type, CmdlineError>(
                Type::help(argc, argv, faces));
        }

        return Type::parse(argc, argv, faces);
    }
}

// Parses argv, printing usage for help or a diagnostic for errors.
template <FixedString kSpec, SpecTypeList... Lists>
auto parse_cmdline(
    int argc, const char* const argv[], std::span<const UsageFace> faces = {}) {
    constexpr auto spec = checked_spec<kSpec>();
    if constexpr (!spec.valid) {
        return detail::InvalidCmdline{};
    } else {
        using Spec = TypedSpec<spec, Lists...>;
        using Type = detail::Cmdline<Spec>;
        static_assert(Spec::defaults_valid);
        std::expected<Type, CmdlineError> runtime =
            detail::argv_requests_help(argc, argv)
                ? std::expected<Type, CmdlineError>(
                      Type::help(argc, argv, faces))
                : Type::parse(argc, argv, faces);

        if (runtime && runtime->help_requested()) {
            runtime->usage(stdout);
            std::exit(EXIT_SUCCESS);
        }

        if (runtime) {
            runtime->print_warnings(stderr);
            return std::move(*runtime);
        }

        detail::exit_with_error(runtime.error(), argc, argv, faces);
    }
}

}  // namespace cmd
