#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "cmdline_error.h"
#include "cmdline_faces.h"
#include "cmdline_model.h"
#include "cmdline_parse.h"
#include "cmdline_print.h"
#include "cmdline_rules.h"
#include "cmdline_typed.h"

// Bridges a type-resolved compile-time spec to one runtime argv invocation.
// TypedSpec has already parsed the spec, checked it, selected C++ value types,
// and defined the accessor result types. This file turns that static metadata
// into owned runtime strings, matches argv tokens, keeps source locations for
// diagnostics, then converts retained values into the typed accessor storage.
//
// Matching is intentionally split into two phases:
//
//   1. process() walks argv and records raw strings plus ValueLocation data.
//   2. materialize() converts raw strings using the resolved Type parsers.
//
// Keeping those phases separate lets --help skip required argument checks, lets
// ignored repeat values remain unparsed, and gives runtime errors enough
// context to point at argv, environment variables, or defaults.

namespace cmd::detail {

// Splits text on a delimiter without trimming or unescaping the parts.
inline std::vector<std::string> split_text(
    std::string_view text, char delimiter) {
    std::vector<std::string> values;
    size_t offset = 0;
    while (offset <= text.size()) {
        size_t next = text.find(delimiter, offset);
        if (next == std::string_view::npos) {
            next = text.size();
        }

        std::string_view value = text.substr(offset, next - offset);
        values.emplace_back(value);
        if (next == text.size()) {
            break;
        }

        offset = next + 1;
    }
    return values;
}

// Splits a pipe-separated option choice list.
inline std::vector<std::string> split_choices(std::string_view text) {
    return split_text(text, '|');
}

// Takes one fallback string from an environment variable or default tag and
// returns the value list to apply to an option. Repeat-all value options split
// on commas, while scalar options preserve the whole string.
inline std::vector<std::string> fallback_values(
    const Option& option, std::string_view text) {
    if (option.takes_value && option.repeat == RepeatPolicy::kAll) {
        return split_text(text, ',');
    }
    return {std::string(text)};
}

// Returns the user-facing source name for one option value.
inline std::string option_value_context(
    const Option& option, const ValueLocation& location) {
    if (location.origin == ValueOrigin::kEnvironment &&
        !option.env_name.empty()) {
        std::string context = "environment variable ";
        context += option.env_name;
        return context;
    }

    if (location.origin == ValueOrigin::kDefault) {
        std::string context = "default for ";
        context += option.full_name();
        return context;
    }
    return option.full_name();
}

// Checks one retained option value against its explicit choice list. Options
// without choices always pass. Failures preserve the value location so runtime
// diagnostics can point at argv, defaults, or environment context.
inline std::expected<void, CmdlineError> check_option_choice(
    const Option& option, std::string_view value,
    const ValueLocation& location) {
    if (option.choices.empty() ||
        std::ranges::find(option.choices, value) != option.choices.end()) {
        return {};
    }

    return std::unexpected(
        CmdlineError{
            .code = ErrorCode::kBadArgument,
            .info = choice_info(
                option_value_context(option, location), value, option.choices),
            .location = location,
        });
}

// Builds the argv spelling for one short option name.
inline std::string short_option_token(char name) {
    return {'-', name};
}

// Returns whether argv contains a built-in help spelling before '--'.
inline bool argv_requests_help(int argc, const char* const argv[]) {
    if (argv == nullptr) {
        return false;
    }

    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            continue;
        }

        std::string_view token(argv[index]);
        if (token == "--") {
            return false;
        }

        if (is_help_token(token)) {
            return true;
        }

        if (short_block_has_help(token)) {
            return true;
        }
    }
    return false;
}

// Walks argv tokens while preserving original argv indexes for diagnostics.
// argv is already shell-tokenized, so this cursor only needs peek/drop helpers
// rather than a character-level parser.
class Argv {
 public:
    // Stores the argv array and starts at the first user argument.
    Argv(int argc, const char* const argv[]) : _argc(argc), _argv(argv) {}

    // Returns whether no user tokens remain.
    bool done() const { return _index >= _argc; }

    // Returns whether a token exists at the current cursor plus `offset`.
    bool has(int offset = 0) const {
        int index = _index + offset;
        return index >= 0 && index < _argc && _argv != nullptr &&
               _argv[index] != nullptr;
    }

    // Returns the current token plus `offset`, or empty text if absent.
    std::string_view peek(int offset = 0) const {
        if (!has(offset)) {
            return {};
        }
        return _argv[_index + offset];
    }

    // Returns the original argv index of the current token.
    size_t index() const { return static_cast<size_t>(_index); }

    // Advances the cursor by `count` tokens.
    void drop(int count = 1) { _index += count; }

 private:
    int _argc                = 0;
    const char* const* _argv = nullptr;
    int _index               = 1;
};

// Describes the result of trying one argv parser rule. Rules report how many
// tokens they consumed, while accept() applies cursor movement centrally.
struct Step {
    int consumed = 0;

    // Returns a no-match parser step.
    static Step no_match() { return {}; }

    // Returns a parser step that consumed argv tokens.
    static Step consumed_tokens(int count) { return {count}; }

    // Returns whether the rule matched the current token.
    bool matched() const { return consumed != 0; }
};

// Stores raw strings captured for one positional argument. These remain
// unconverted until materialize() so structural matching and type parsing stay
// separate.
struct RawArg {
    std::vector<std::string> values;
    std::vector<ValueLocation> value_locations;
};

// Stores raw strings and occurrence state for one option. `is_set` is the
// effective option state after repeat policy is applied, while `specified`
// tracks whether argv mentioned the option even if an ignored repeat value was
// not retained.
struct RawOpt {
    bool is_set    = false;
    bool specified = false;
    int count      = 0;
    std::vector<std::string> values;
    std::vector<ValueLocation> value_locations;
};

// Stores the complete argv match result before typed materialization. This is
// the boundary between token matching and value conversion.
struct RawCmdline {
    bool help_requested = false;
    std::vector<RawArg> args;
    std::vector<RawOpt> opts;
    std::vector<CmdlineError> warnings;
};

// Stores one parsed invocation. Runtime metadata comes from Spec, raw state is
// filled by process(), and typed data slots are filled by materialize().
template <typename Spec>
class Cmdline {
 public:
    using ArgDataTuple = typename Spec::ArgDataTuple;
    using OptDataTuple = typename Spec::OptDataTuple;

    template <FixedString kName>
    using ArgResult = typename Spec::template ArgResult<kName>;

    template <FixedString kName>
    using OptionResult = typename Spec::template OptResult<kName>;

    template <size_t kIndex>
    using ArgData = typename Spec::template ArgData<kIndex>;

    template <size_t kIndex>
    using OptData = typename Spec::template OptData<kIndex>;

    template <typename Value>
    using DataSlot = typename Spec::template DataSlot<Value>;

    // Builds a command line whose only parsed state is help requested. This is
    // used by the public help escape hatch before normal argv validation.
    static Cmdline help(
        int argc, const char* const argv[],
        std::span<const UsageFace> faces = {}) {
        Cmdline cmdline(program_name(argc, argv), faces);
        cmdline._raw.help_requested = true;
        return cmdline;
    }

    // Parses `argv` using a type-resolved spec checked at compile time. The
    // result owns both the printable spec model and the typed accessor values.
    static std::expected<Cmdline, CmdlineError> parse(
        int argc, const char* const argv[],
        std::span<const UsageFace> faces = {}) {
        Cmdline cmdline(program_name(argc, argv), faces);

        std::expected<void, CmdlineError> result = cmdline.process(argc, argv);
        if (!result) {
            return std::unexpected(result.error());
        }

        if (!cmdline.help_requested()) {
            result = cmdline.materialize();
            if (!result) {
                return std::unexpected(result.error());
            }
        }
        return cmdline;
    }

    // Returns whether a built-in help spelling was present.
    bool help_requested() const { return _raw.help_requested; }

    // Prints generated usage text to `file`.
    void usage(FILE* file = stdout) const {
        print_usage(file, _program, _lines, _args, _faces);
    }

    // Returns non-fatal warnings encountered while parsing values.
    const std::vector<CmdlineError>& warnings() const { return _raw.warnings; }

    // Prints all non-fatal runtime warnings to `file`.
    void print_warnings(FILE* file = stderr) const {
        for (const CmdlineError& warning : _raw.warnings) {
            fmt::print(
                file, "{}",
                format_cmdline_warning(warning, _faces, color_enabled(file)));
        }
    }

    // Returns the declared positional arguments.
    const std::vector<PosArg>& args() const { return _args; }

    // Returns the declared options and switches.
    const std::vector<Option>& opts() const { return _opts; }

    // Returns the runtime face map used for generated diagnostics.
    const FaceMap& faces() const { return _faces; }

    // Returns a typed positional value or vector for a variadic argument.
    template <FixedString kName>
    const ArgResult<kName>& arg() const;

    // Returns a typed option value, switch state, or count.
    template <FixedString kName>
    const OptionResult<kName>& opt() const;

    // Returns whether an option spelling was present on argv.
    template <FixedString kName>
    bool set() const;

 private:
    // Records a non-fatal runtime warning.
    void warn(CmdlineError warning) {
        _raw.warnings.push_back(std::move(warning));
    }

    // Builds runtime metadata and stores the originating program name. This
    // copies the compile-time spec into owned strings, then indexes option
    // spellings for runtime matching.
    Cmdline(std::string program, std::span<const UsageFace> faces)
        : _program(std::move(program)), _faces(default_faces(faces)) {
        add_lines(std::make_index_sequence<Spec::nline>{});
        index_options();
        _raw.args.resize(_args.size());
        _raw.opts.resize(_opts.size());
    }

    // Parses the recorded values for one option using the resolved value type.
    template <SpecType Type>
    std::expected<std::vector<typename Type::type>, CmdlineError>
    parse_option_values(const Option& option, const RawOpt& raw);

    // Materializes one positional argument data value from raw argv values.
    template <size_t kIndex>
    std::expected<ArgData<kIndex>, CmdlineError> parse_arg_data() const;

    // Materializes one option data value from raw argv values and fallbacks.
    template <size_t kIndex>
    std::expected<OptData<kIndex>, CmdlineError> parse_opt_data();

    // Materializes every positional argument into data slots.
    template <size_t kIndex = 0, typename... Values>
    std::expected<ArgDataTuple, CmdlineError> parse_arg_data_all(
        Values&&... values) const;

    // Materializes every option into data slots.
    template <size_t kIndex = 0, typename... Values>
    std::expected<OptDataTuple, CmdlineError> parse_opt_data_all(
        Values&&... values);

    // Returns the originating program name from argv.
    static std::string program_name(int argc, const char* const argv[]) {
        if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
            return argv[0];
        }
        return "";
    }

    // Finds the declared option index matched by one argv token. Accepted
    // spellings first resolve to a canonical token, then canonical tokens map
    // to the stable option index used by raw state and typed data tuples.
    std::optional<size_t> find_option_index(std::string_view token) const {
        auto name = _option_names.find(std::string(token));
        if (name == _option_names.end()) {
            return {};
        }

        auto option = _option_index.find(name->second);
        if (option == _option_index.end() || option->second >= _opts.size()) {
            return {};
        }

        return option->second;
    }

    // Returns the canonical lookup token for an option.
    static std::string canonical_token(const Option& option) {
        if (!option.long_name.empty()) {
            return option.long_form();
        }
        return option.short_form();
    }

    // Adds one accepted argv token to the canonical-token map. This keeps
    // --long, -s, and --no-long spellings from needing separate option records.
    void index_token(std::string token, const std::string& canonical) {
        _option_names.insert_or_assign(std::move(token), canonical);
    }

    // Adds all accepted argv spellings for one option to the lookup maps.
    void index_option(size_t index) {
        const Option& option  = _opts[index];
        std::string canonical = canonical_token(option);
        _option_index.insert_or_assign(canonical, index);

        index_token(canonical, canonical);
        if (!option.short_name.empty()) {
            index_token(option.short_form(), canonical);
        }

        if (option.has_negative_form()) {
            index_token("--no-" + option.long_name, canonical);
        }
    }

    // Builds token lookup maps after all options have been materialized. The
    // matcher then does bounded hash lookups instead of scanning every option
    // for every argv token.
    void index_options() {
        _option_index.clear();
        _option_names.clear();
        _option_index.reserve(_opts.size());
        _option_names.reserve(_opts.size() * 3);

        for (size_t index = 0; index < _opts.size(); ++index) {
            index_option(index);
        }
    }

    // Copies a compile-time range into a runtime string.
    static std::string to_string(TextRange range) {
        return std::string(range.view(Spec::raw.source));
    }

    // Copies a description range while unescaping literal brackets.
    static std::string to_description(TextRange range) {
        std::string_view text = range.view(Spec::raw.source);
        std::string output;
        output.reserve(text.size());

        for (size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '\\' && index + 1 < text.size() &&
                (text[index + 1] == '[' || text[index + 1] == ']')) {
                output.push_back(text[index + 1]);
                ++index;
                continue;
            }

            output.push_back(text[index]);
        }
        return output;
    }

    // Copies a default value range while unescaping quoted default escapes.
    static std::string to_default_value(TextRange range, bool unescape) {
        std::string_view text = range.view(Spec::raw.source);
        std::string output;
        output.reserve(text.size());

        for (size_t index = 0; index < text.size(); ++index) {
            if (unescape && text[index] == '\\' && index + 1 < text.size() &&
                (text[index + 1] == '"' || text[index + 1] == '\\')) {
                output.push_back(text[index + 1]);
                ++index;
                continue;
            }

            output.push_back(text[index]);
        }
        return output;
    }

    // Converts one compile-time positional argument into runtime metadata. The
    // selected Type contributes the C++ type name, while the original spec type
    // name remains available as the face name for usage styling.
    template <size_t kIndex>
    static PosArg make_arg() {
        using Type             = typename Spec::template ArgType<kIndex>;
        constexpr ArgSpec spec = Spec::arg_spec(kIndex);

        PosArg arg;
        arg.name         = to_string(spec.name);
        arg.type_name    = std::string(Type::name);
        arg.value_label  = arg.name;
        arg.face_name    = std::string(Spec::raw.arg_type_name(kIndex));
        arg.description  = to_description(spec.description);
        arg.accepts_many = spec.variadic;
        arg.required     = spec.required;
        return arg;
    }

    // Converts one compile-time option into runtime metadata. Choices are
    // runtime string values, so they deliberately override the resolved type
    // display metadata back to the default string type.
    template <size_t kIndex>
    static Option make_option() {
        using Type             = typename Spec::template OptType<kIndex>;
        constexpr OptSpec spec = Spec::opt_spec(kIndex);

        Option option;
        option.long_name        = to_string(spec.long_name);
        option.short_name       = to_string(spec.short_name);
        option.description      = to_description(spec.description);
        option.type_name        = std::string(Type::name);
        option.value_label_name = to_string(spec.value_name);
        option.default_value =
            to_default_value(spec.default_value, spec.default_quoted);
        option.env_name        = to_string(spec.env_name);
        option.value_face_name = std::string(Spec::raw.opt_type_name(kIndex));
        if (!spec.choices.empty()) {
            option.choices          = split_choices(to_string(spec.choices));
            option.type_name        = std::string(kDefaultTypeName);
            option.value_label_name = "";
            option.value_face_name  = std::string(kDefaultTypeName);
        }
        option.repeat            = spec.repeat;
        option.takes_value       = spec.takes_value;
        option.count_occurrences = spec.counting;
        option.required          = spec.required;
        option.hidden            = spec.hidden;
        return option;
    }

    // Adds one compile-time checked line to the runtime representation. Lines
    // preserve declaration order because usage output follows the user's spec.
    template <size_t kLine>
    void add_line() {
        constexpr LineSpec line = Spec::raw.lines[kLine];
        if constexpr (line.type == LineType::kBlank) {
            _lines.push_back(SpecLine::make_blank());

        } else if constexpr (line.type == LineType::kHeading) {
            _lines.push_back(
                SpecLine::make_heading(to_string(line.text), line.indent));

        } else if constexpr (line.type == LineType::kRawText) {
            _lines.push_back(
                SpecLine::make_raw_text(to_string(line.text), line.indent));

        } else if constexpr (line.type == LineType::kArg) {
            PosArg arg = make_arg<line.index>();
            _args.push_back(arg);
            _lines.push_back(
                SpecLine::make_pos_arg(std::move(arg), line.indent));

        } else if constexpr (line.type == LineType::kOption) {
            Option option = make_option<line.index>();
            _opts.push_back(option);
            _lines.push_back(
                SpecLine::make_option(std::move(option), line.indent));
        }
    }

    // Adds all compile-time checked lines to the runtime representation.
    template <size_t... Lines>
    void add_lines(std::index_sequence<Lines...>) {
        (add_line<Lines>(), ...);
    }

    // Builds a runtime error that points at an argv token or the line end.
    static CmdlineError make_error(
        ErrorCode code, CmdlineErrorInfo info, size_t argument_index,
        size_t argument_offset = 0) {
        return CmdlineError{
            .code     = code,
            .info     = std::move(info),
            .location = ValueLocation::argv(argument_index, argument_offset),
        };
    }

    // Builds a runtime error from a recorded value source.
    static CmdlineError make_error(
        ErrorCode code, CmdlineErrorInfo info, const ValueLocation& location) {
        return CmdlineError{
            .code     = code,
            .info     = std::move(info),
            .location = location,
        };
    }

    // Accepts a rule result and advances argv when the rule matched. Rules
    // describe what happened, while this helper owns cursor movement.
    std::expected<bool, CmdlineError> accept(
        std::expected<Step, CmdlineError> step) {
        if (!step) {
            return std::unexpected(step.error());
        }

        if (!step->matched()) {
            return false;
        }

        _argv.drop(step->consumed);
        return true;
    }

    // Expects a rule result to match and advances argv after success. This is
    // used for the fallback positional path where failure means the token was
    // not recognized by any rule.
    std::expected<void, CmdlineError> expect(
        std::expected<Step, CmdlineError> step) {
        std::expected<bool, CmdlineError> matched = accept(std::move(step));
        if (!matched) {
            return std::unexpected(matched.error());
        }

        if (!*matched) {
            return std::unexpected(make_error(
                ErrorCode::kBadArgument,
                value_info(CmdlineErrorType::kUnexpectedArgument, _argv.peek()),
                _argv.index()));
        }
        return {};
    }

    // Matches the `--` option terminator.
    std::expected<Step, CmdlineError> stop_token() {
        if (!_positional_only && _argv.peek() == "--") {
            _positional_only = true;
            return Step::consumed_tokens(1);
        }
        return Step::no_match();
    }

    // Matches an option or short-switch block at the current token. A bare "-"
    // is positional, and "--" is handled by stop_token() before this rule.
    std::expected<Step, CmdlineError> option_token() {
        std::string_view token = _argv.peek();
        if (_positional_only || token == "-" || !token.starts_with("-")) {
            return Step::no_match();
        }

        if (is_short_switch_block(token)) {
            return consume_short_switch_block(token);
        }
        return consume_option(token);
    }

    // Matches a positional argument at the current token.
    std::expected<Step, CmdlineError> argument_token() {
        std::expected<void, CmdlineError> result =
            consume_positional(_argv.peek());
        if (!result) {
            return std::unexpected(result.error());
        }
        return Step::consumed_tokens(1);
    }

    // Returns whether one token is a block of single-letter switches. Blocks
    // are accepted only when every character names a valueless switch.
    bool is_short_switch_block(std::string_view token) {
        if (token.size() <= 2 || token.front() != '-' || token[1] == '-') {
            return false;
        }

        for (size_t pos = 1; pos < token.size(); ++pos) {
            if (is_help_short_name(token[pos])) {
                continue;
            }

            std::string short_token     = short_option_token(token[pos]);
            std::optional<size_t> index = find_option_index(short_token);
            if (!index || _opts[*index].takes_value) {
                return false;
            }
        }
        return true;
    }

    // Consumes one token as a block of single-letter switches.
    std::expected<Step, CmdlineError> consume_short_switch_block(
        std::string_view token) {
        for (size_t pos = 1; pos < token.size(); ++pos) {
            if (is_help_short_name(token[pos])) {
                _raw.help_requested = true;
                continue;
            }

            std::string short_token     = short_option_token(token[pos]);
            std::optional<size_t> index = find_option_index(short_token);
            if (!index) {
                return std::unexpected(  //
                    make_error(
                        ErrorCode::kUnknown,
                        subject_info(
                            CmdlineErrorType::kUnknownOption, short_token),
                        _argv.index(), pos));
            }

            const Option& option = _opts[*index];
            RawOpt& raw          = _raw.opts[*index];
            std::expected<void, CmdlineError> result =
                set_switch(option, raw, true, _argv.index(), pos);
            if (!result) {
                return std::unexpected(result.error());
            }
        }
        return Step::consumed_tokens(1);
    }

    // Consumes one option token and any required following value. Long and
    // short value options support both a following token and the explicit
    // "=value" form, while attached short values such as -ovalue are rejected
    // elsewhere.
    std::expected<Step, CmdlineError> consume_option(std::string_view token) {
        std::string option_token(token);
        std::optional<std::string> inline_value;
        size_t equals = option_token.find('=');
        if (equals != std::string::npos) {
            inline_value = option_token.substr(equals + 1);
            option_token.erase(equals);
        }

        std::optional<size_t> index = find_option_index(option_token);
        if (!index && is_help_token(option_token)) {
            if (inline_value) {
                return std::unexpected(make_error(
                    ErrorCode::kBadArgument,
                    subject_info(
                        CmdlineErrorType::kNoValueAllowed, option_token),
                    _argv.index(), equals));
            }
            _raw.help_requested = true;
            return Step::consumed_tokens(1);
        }

        if (!index) {
            return std::unexpected(make_error(
                ErrorCode::kUnknown,
                subject_info(CmdlineErrorType::kUnknownOption, token),
                _argv.index()));
        }

        const Option& option = _opts[*index];
        RawOpt& raw          = _raw.opts[*index];
        if (!option.takes_value) {
            return consume_switch_option(
                option, raw, option_token, inline_value);
        }
        return consume_value_option(
            option, raw, option_token, inline_value, equals);
    }

    // Consumes one option token that does not take a value.
    std::expected<Step, CmdlineError> consume_switch_option(
        const Option& option, RawOpt& raw, std::string_view option_token,
        const std::optional<std::string>& inline_value) {
        if (inline_value) {
            return std::unexpected(make_error(
                ErrorCode::kBadArgument,
                subject_info(CmdlineErrorType::kNoValueAllowed, option_token),
                _argv.index(), option_token.size()));
        }

        bool enabled = !option_token.starts_with("--no-");
        std::expected<void, CmdlineError> result =
            set_switch(option, raw, enabled, _argv.index());
        if (!result) {
            return std::unexpected(result.error());
        }
        return Step::consumed_tokens(1);
    }

    // Consumes one option token that takes a value. If no inline value is
    // present the next argv token is consumed greedily, even when that token is
    // dash-prefixed or exactly "--".
    std::expected<Step, CmdlineError> consume_value_option(
        const Option& option, RawOpt& raw, std::string_view option_token,
        const std::optional<std::string>& inline_value, size_t equals) {
        std::string value;
        size_t value_index  = _argv.index();
        size_t value_offset = 0;
        int skip            = 1;
        if (inline_value) {
            value        = *inline_value;
            value_offset = equals + 1;

        } else {
            if (!_argv.has(1)) {
                return std::unexpected(make_error(
                    ErrorCode::kIncomplete,
                    subject_info(
                        CmdlineErrorType::kValueRequired, option_token),
                    _argv.index()));
            }

            value_index = _argv.index() + 1;
            value       = _argv.peek(1);
            skip        = 2;
        }

        std::expected<void, CmdlineError> result = set_value(
            option, raw, value, ValueLocation::argv(value_index, value_offset));
        if (!result) {
            return std::unexpected(result.error());
        }
        return Step::consumed_tokens(skip);
    }

    // Consumes one positional argument token. Fixed positionals advance the
    // current argument index, while a final variadic positional keeps accepting
    // remaining values.
    std::expected<void, CmdlineError> consume_positional(
        std::string_view token) {
        const std::vector<PosArg>& args = _args;
        if (_arg_index >= args.size()) {
            if (!args.empty() && args.back().accepts_many) {
                RawArg& raw = _raw.args.back();
                raw.values.push_back(std::string(token));
                raw.value_locations.push_back(
                    ValueLocation::argv(_argv.index()));
                return {};
            }

            return std::unexpected(make_error(
                ErrorCode::kOverflow,
                value_info(CmdlineErrorType::kUnexpectedPositional, token),
                _argv.index()));
        }

        const PosArg& arg = args[_arg_index];
        RawArg& raw       = _raw.args[_arg_index];
        raw.values.push_back(std::string(token));
        raw.value_locations.push_back(ValueLocation::argv(_argv.index()));
        if (!arg.accepts_many) {
            ++_arg_index;
        }
        return {};
    }

    // Records a switch occurrence. Count switches accumulate, first/last
    // switches apply repeat policy, and ordinary switches reject repeats.
    std::expected<void, CmdlineError> set_switch(
        const Option& option, RawOpt& raw, bool enabled, size_t argument_index,
        size_t argument_offset = 0) {
        if (option.count_occurrences) {
            raw.is_set    = true;
            raw.specified = true;
            ++raw.count;
            return {};
        }

        if (option.repeat == RepeatPolicy::kError && raw.count != 0) {
            return std::unexpected(make_error(
                ErrorCode::kAlreadySet,
                subject_info(
                    CmdlineErrorType::kRepeatedOption, option.full_name()),
                argument_index, argument_offset));
        }

        if (option.repeat == RepeatPolicy::kFirst && raw.count != 0) {
            raw.specified = true;
            return {};
        }

        raw.is_set    = enabled;
        raw.specified = true;
        ++raw.count;
        return {};
    }

    // Records one option value. Only retained values are type-checked later, so
    // ignored first/last repeats can safely contain values invalid for the
    // type.
    std::expected<void, CmdlineError> set_value(
        const Option& option, RawOpt& raw, std::string value,
        ValueLocation location) {
        if (option.repeat == RepeatPolicy::kError && raw.is_set) {
            return std::unexpected(make_error(
                ErrorCode::kAlreadySet,
                subject_info(
                    CmdlineErrorType::kRepeatedOption, option.full_name()),
                location));
        }

        if (option.repeat == RepeatPolicy::kFirst && raw.is_set) {
            if (location.origin == ValueOrigin::kArgv) {
                raw.specified = true;
            }
            ++raw.count;
            return {};
        }

        if (option.repeat == RepeatPolicy::kLast && raw.is_set) {
            raw.values.clear();
            raw.value_locations.clear();
        }

        raw.is_set = true;
        if (location.origin == ValueOrigin::kArgv) {
            raw.specified = true;
        }
        ++raw.count;
        raw.values.push_back(std::move(value));
        raw.value_locations.push_back(location);
        return {};
    }

    // Checks retained argv option values against explicit choice lists before
    // materialization. Environment and default values are checked later when
    // they are selected as fallbacks.
    std::expected<void, CmdlineError> validate_choices() const {
        for (size_t opt = 0; opt < _opts.size(); ++opt) {
            const Option& option = _opts[opt];
            const RawOpt& raw    = _raw.opts[opt];
            for (size_t i = 0; i < raw.values.size(); ++i) {
                ValueLocation location;
                if (i < raw.value_locations.size()) {
                    location = raw.value_locations[i];
                }

                std::expected<void, CmdlineError> choice =
                    check_option_choice(option, raw.values[i], location);
                if (!choice) {
                    return std::unexpected(choice.error());
                }
            }
        }
        return {};
    }

    // Checks final retained argv parse state before typed materialization. Help
    // is an escape hatch, so missing required values do not block usage output.
    std::expected<void, CmdlineError> finish(int argc) {
        std::expected<void, CmdlineError> choices = validate_choices();
        if (!choices) {
            return std::unexpected(choices.error());
        }

        if (_raw.help_requested) {
            return {};
        }

        for (size_t arg = 0; arg < _args.size(); ++arg) {
            const PosArg& spec = _args[arg];
            const RawArg& raw  = _raw.args[arg];
            if (spec.required && raw.values.empty()) {
                return std::unexpected(make_error(
                    ErrorCode::kUnderflow,
                    subject_info(CmdlineErrorType::kMissingArgument, spec.name),
                    argc));
            }
        }

        return {};
    }

    // Consumes argv tokens and records matched argument and option values. The
    // rule order is "--" terminator, option token, then positional token.
    std::expected<void, CmdlineError> process(
        int argc, const char* const argv[]) {
        _argv = Argv(argc, argv);
        while (!_argv.done()) {
            std::expected<bool, CmdlineError> stopped = accept(stop_token());
            if (!stopped) {
                return std::unexpected(stopped.error());
            }
            if (*stopped) {
                continue;
            }

            std::expected<bool, CmdlineError> optioned = accept(option_token());
            if (!optioned) {
                return std::unexpected(optioned.error());
            }
            if (*optioned) {
                continue;
            }

            std::expected<void, CmdlineError> argued = expect(argument_token());
            if (!argued) {
                return std::unexpected(argued.error());
            }
        }
        return finish(argc);
    }

    // Materializes raw argv state into typed accessor data. This is called only
    // after token matching succeeds and help was not requested.
    std::expected<void, CmdlineError> materialize();

    // Runtime copy of the checked spec, stored as owned strings for usage and
    // diagnostics.
    std::string _program;
    std::vector<SpecLine> _lines;
    std::vector<PosArg> _args;
    std::vector<Option> _opts;

    // Lookup indexes used while matching argv option tokens.
    std::unordered_map<std::string, size_t> _option_index;
    std::unordered_map<std::string, std::string> _option_names;

    // Runtime formatting policy and raw parse state.
    FaceMap _faces;
    RawCmdline _raw;
    Argv _argv{0, nullptr};

    // Typed accessor storage filled after raw matching succeeds.
    ArgDataTuple _arg_data;
    OptDataTuple _opt_data;

    // Cursor state used only while process() is walking argv.
    size_t _arg_index     = 0;
    bool _positional_only = false;
};

// Returns a filled slot value, or an inert fallback for help-only results.
template <typename T>
const T& value_or_empty(const std::optional<T>& slot) {
    if (slot) {
        return *slot;
    }

    static const T empty{};
    return empty;
}

// Builds a runtime typed-parse error for one parsed value.
inline CmdlineError make_typed_value_error(
    std::string_view name, std::string_view type_name, std::string_view value,
    const ValueLocation& location, std::string_view reason) {
    return CmdlineError{
        .code     = ErrorCode::kBadArgument,
        .info     = typed_value_info(name, type_name, value, reason),
        .location = location,
    };
}

// Returns the recorded location for a raw positional value.
inline ValueLocation arg_value_location(const RawArg& raw, size_t index) {
    if (index < raw.value_locations.size()) {
        return raw.value_locations[index];
    }
    return {};
}

// Stores borrowed option values and their source locations for typed parsing.
struct OptionValueView {
    std::span<const std::string> values;
    std::span<const ValueLocation> locations;
};

// Owns fallback option values long enough to expose a borrowed value view.
struct OwnedOptionValues {
    std::vector<std::string> values;
    std::vector<ValueLocation> locations;

    // Returns a view over the owned fallback values.
    OptionValueView view() const { return {values, locations}; }
};

// Borrows already recorded option values for typed parsing.
inline OptionValueView recorded_values(const RawOpt& raw) {
    return {raw.values, raw.value_locations};
}

// Builds owned fallback values and source locations using one location policy.
template <typename MakeLocation>
inline OwnedOptionValues fallback_option_values(
    const Option& option, std::string_view text, MakeLocation location_for) {
    OwnedOptionValues raw;
    for (const std::string& item : fallback_values(option, text)) {
        raw.locations.push_back(location_for(item));
        raw.values.push_back(item);
    }
    return raw;
}

// Builds default option values for typed parsing after an env fallback fails.
inline OwnedOptionValues default_values(const Option& option) {
    return fallback_option_values(
        option, option.default_value, [](std::string_view value) {
            return ValueLocation::default_value(value);
        });
}

// Builds environment option values as typed parsing candidates.
inline OwnedOptionValues environment_values(
    const Option& option, std::string_view value) {
    return fallback_option_values(
        option, value, [&option](std::string_view item) {
            return ValueLocation::environment(option.env_name, item);
        });
}

// Parses one typed value and converts parser failures to CmdlineError.
template <SpecType Type>
std::expected<typename Type::type, CmdlineError> parse_typed_value(
    std::string_view name, std::string_view value,
    const ValueLocation& location) {
    auto parsed = Type::parse(value);
    if (!parsed) {
        return std::unexpected(make_typed_value_error(
            name, Type::name, value, location, parsed.error().error_message()));
    }
    return std::move(*parsed);
}

// Parses positional values with one selected command-line type.
template <SpecType Type>
std::expected<std::vector<typename Type::type>, CmdlineError> parse_arg_values(
    const PosArg& arg, const RawArg& raw) {
    std::vector<typename Type::type> result;
    result.reserve(raw.values.size());
    for (size_t i = 0; i < raw.values.size(); ++i) {
        std::expected<typename Type::type, CmdlineError> value =
            parse_typed_value<Type>(
                arg.name, raw.values[i], arg_value_location(raw, i));
        if (!value) {
            return std::unexpected(value.error());
        }

        result.push_back(std::move(*value));
    }
    return result;
}

// Parses option values with one selected command-line type.
template <SpecType Type>
std::expected<std::vector<typename Type::type>, CmdlineError>
parse_option_value_view(const Option& option, OptionValueView raw) {
    std::vector<typename Type::type> result;
    result.reserve(raw.values.size());
    for (size_t i = 0; i < raw.values.size(); ++i) {
        ValueLocation location;
        if (i < raw.locations.size()) {
            location = raw.locations[i];
        }

        if (location.origin != ValueOrigin::kArgv) {
            std::expected<void, CmdlineError> choice =
                check_option_choice(option, raw.values[i], location);
            if (!choice) {
                return std::unexpected(choice.error());
            }
        }

        std::string context = option_value_context(option, location);
        std::expected<typename Type::type, CmdlineError> value =
            parse_typed_value<Type>(context, raw.values[i], location);
        if (!value) {
            return std::unexpected(value.error());
        }

        result.push_back(std::move(*value));
    }
    return result;
}

// Parses option values and treats invalid environment fallbacks as absent.
template <typename Spec>
template <SpecType Type>
std::expected<std::vector<typename Type::type>, CmdlineError>
Cmdline<Spec>::parse_option_values(
    const Option& option, const RawOpt& option_values) {
    if (option_values.is_set) {
        return parse_option_value_view<Type>(
            option, recorded_values(option_values));
    }

    if (!option.env_name.empty()) {
        if (const char* value = std::getenv(option.env_name.c_str())) {
            OwnedOptionValues env = environment_values(option, value);
            std::expected<std::vector<typename Type::type>, CmdlineError>
                parsed = parse_option_value_view<Type>(option, env.view());
            if (parsed) {
                return parsed;
            }

            warn(parsed.error());
        }
    }

    if (!option.default_value.empty()) {
        OwnedOptionValues defaults = default_values(option);
        return parse_option_value_view<Type>(option, defaults.view());
    }

    return std::vector<typename Type::type>{};
}

// Parses one positional argument's typed data value.
template <typename Spec>
template <size_t kIndex>
std::expected<typename Cmdline<Spec>::template ArgData<kIndex>, CmdlineError>
Cmdline<Spec>::parse_arg_data() const {
    using Type = typename Spec::template ArgType<kIndex>;

    const PosArg& arg = _args[kIndex];
    const RawArg& raw = _raw.args[kIndex];
    if constexpr (Spec::arg_spec(kIndex).variadic) {
        return parse_arg_values<Type>(arg, raw);
    } else {
        if (raw.values.empty()) {
            return std::unexpected(
                CmdlineError{
                    .code = ErrorCode::kUnderflow,
                    .info = subject_info(
                        CmdlineErrorType::kMissingArgument, arg.name),
                });
        }

        return parse_typed_value<Type>(
            arg.name, raw.values.front(), arg_value_location(raw, 0));
    }
}

// Parses one option or switch's typed data value.
template <typename Spec>
template <size_t kIndex>
std::expected<typename Cmdline<Spec>::template OptData<kIndex>, CmdlineError>
Cmdline<Spec>::parse_opt_data() {
    const Option& option   = _opts[kIndex];
    const RawOpt& raw      = _raw.opts[kIndex];
    constexpr OptSpec spec = Spec::opt_spec(kIndex);
    if constexpr (!spec.takes_value) {
        if constexpr (spec.counting) {
            return raw.count;
        } else {
            return raw.is_set;
        }
    } else {
        using Type  = typename Spec::template OptType<kIndex>;
        using Value = typename Spec::template OptValueType<kIndex>;

        std::expected<std::vector<Value>, CmdlineError> values =
            this->template parse_option_values<Type>(option, raw);
        if (!values) {
            return std::unexpected(values.error());
        }

        if constexpr (spec.keeps_all()) {
            return std::move(*values);
        } else if (values->empty()) {
            if constexpr (spec.is_optional_value()) {
                return std::optional<Value>{};
            } else {
                return std::unexpected(
                    CmdlineError{
                        .code = ErrorCode::kIncomplete,
                        .info = subject_info(
                            CmdlineErrorType::kMissingOptionValue,
                            option.full_name()),
                    });
            }
        } else {
            size_t selected = values->size() - 1;
            if constexpr (spec.repeat == RepeatPolicy::kFirst) {
                selected = 0;
            }

            if constexpr (spec.is_optional_value()) {
                return std::optional<Value>(std::move((*values)[selected]));
            } else {
                return std::move((*values)[selected]);
            }
        }
    }
}

// Recursively parses args without requiring value types to be
// default-constructible.
template <typename Spec>
template <size_t kIndex, typename... Values>
std::expected<typename Cmdline<Spec>::ArgDataTuple, CmdlineError>
Cmdline<Spec>::parse_arg_data_all(Values&&... values) const {
    if constexpr (kIndex == Spec::narg) {
        return ArgDataTuple(std::forward<Values>(values)...);
    } else {
        std::expected<ArgData<kIndex>, CmdlineError> parsed =
            this->template parse_arg_data<kIndex>();
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        return this->template parse_arg_data_all<kIndex + 1>(
            std::forward<Values>(values)...,
            DataSlot<ArgData<kIndex>>(std::move(*parsed)));
    }
}

// Recursively parses options without requiring value types to be
// default-constructible.
template <typename Spec>
template <size_t kIndex, typename... Values>
std::expected<typename Cmdline<Spec>::OptDataTuple, CmdlineError>
Cmdline<Spec>::parse_opt_data_all(Values&&... values) {
    if constexpr (kIndex == Spec::nopt) {
        return OptDataTuple(std::forward<Values>(values)...);
    } else {
        std::expected<OptData<kIndex>, CmdlineError> parsed =
            this->template parse_opt_data<kIndex>();
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        return this->template parse_opt_data_all<kIndex + 1>(
            std::forward<Values>(values)...,
            DataSlot<OptData<kIndex>>(std::move(*parsed)));
    }
}

// Returns a typed positional value or vector for a variadic argument.
template <typename Spec>
template <FixedString kName>
auto Cmdline<Spec>::arg() const -> const ArgResult<kName>& {
    constexpr size_t index = Spec::template arg_index<kName>();
    return value_or_empty(std::get<index>(_arg_data));
}

// Returns a typed option value, switch state, or count.
template <typename Spec>
template <FixedString kName>
auto Cmdline<Spec>::opt() const -> const OptionResult<kName>& {
    if constexpr (Spec::is_help_name(kName.view())) {
        return _raw.help_requested;
    } else {
        constexpr size_t index = Spec::template opt_index<kName>();
        return value_or_empty(std::get<index>(_opt_data));
    }
}

// Returns whether an option spelling was present on argv.
template <typename Spec>
template <FixedString kName>
bool Cmdline<Spec>::set() const {
    if constexpr (Spec::is_help_name(kName.view())) {
        return _raw.help_requested;
    } else {
        constexpr size_t index = Spec::template opt_index<kName>();
        return _raw.opts[index].specified;
    }
}

// Converts runtime parse state into typed accessor data.
template <typename Spec>
std::expected<void, CmdlineError> Cmdline<Spec>::materialize() {
    std::expected<ArgDataTuple, CmdlineError> args = parse_arg_data_all();
    if (!args) {
        return std::unexpected(args.error());
    }

    std::expected<OptDataTuple, CmdlineError> options = parse_opt_data_all();
    if (!options) {
        return std::unexpected(options.error());
    }

    _arg_data = std::move(*args);
    _opt_data = std::move(*options);
    return {};
}

}  // namespace cmd::detail
