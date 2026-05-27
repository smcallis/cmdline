#include <expected>

#include <gtest/gtest.h>

#include "internal/cmdline_check.h"

namespace cmd {

namespace {

constexpr char kSpecText[] = R"(
    sync files.

    _Arguments
      <source:path>       Source path.
      <destination:path>  Destination path.
      <include:path>+     Extra include paths.

    _Options
      --config/-c=<path>        Config file. [env: SYNC_CONFIG]
      --format/-f=[json|yaml]   Output format. [default: json]
      --profile=<name>          Named profile.
      --tag/-t=<name>+          Repeatable tags.
      --dry-run                 Validate without copying.
      --quiet                   Quiet flag. [hidden]
      --feature>                Toggleable feature flag.
      --cache=<path>>           Last cache path wins.
      --token=                  Access token. [required]
      --verbose/-v+             Increase verbosity.
      --old-token=              Old token.
)";

// Parses and checks a spec while preserving expected-style errors for tests.
template <FixedString kSpecText>
constexpr ParseStatus parse_and_check_spec() {
    constexpr auto parsed = parse_spec<kSpecText>();
    if constexpr (!parsed.has_value()) {
        return std::unexpected(parsed.error());
    } else {
        return check_spec(*parsed);
    }
}

constexpr auto kParsed = parse_spec<kSpecText>();
static_assert(kParsed.has_value());

constexpr auto kSpec = *kParsed;
static_assert(kSpec.narg == 3);
static_assert(kSpec.nopt == 11);
static_assert(kSpec.lines[2].type == LineType::kHeading);
static_assert(kSpec.args[2].variadic);
static_assert(kSpec.opts[3].repeat == RepeatPolicy::kAll);
static_assert(kSpec.opts[5].hidden);
static_assert(kSpec.opts[6].repeat == RepeatPolicy::kLast);
static_assert(kSpec.opts[7].repeat == RepeatPolicy::kLast);
static_assert(kSpec.opts[8].takes_value);
static_assert(kSpec.opts[8].value_type == ValueType::kString);
static_assert(kSpec.opts[8].required);
static_assert(kSpec.opts[9].counting);

TEST(CmdlineParserTest, ParsesCompileTimeSpecMetadata) {
    EXPECT_EQ(kSpec.narg, 3);
    EXPECT_EQ(kSpec.nopt, 11);
    EXPECT_EQ(kSpec.lines[2].text.view(kSpec.source), "Arguments");
    EXPECT_EQ(kSpec.args[0].name.view(kSpec.source), "source");
    EXPECT_EQ(kSpec.args[0].type.view(kSpec.source), "path");
    EXPECT_TRUE(kSpec.args[2].variadic);
    EXPECT_EQ(kSpec.opts[3].repeat, RepeatPolicy::kAll);
    EXPECT_EQ(kSpec.opts[6].repeat, RepeatPolicy::kLast);
    EXPECT_TRUE(kSpec.opts[9].counting);
}

TEST(CmdlineParserTest, IgnoresInitialBlankLine) {
    constexpr auto spec = parse_spec<R"(
        _Arguments
          <input>
)">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->nline, 3);
    EXPECT_EQ(spec->lines[0].type, LineType::kHeading);
    EXPECT_EQ(spec->lines[0].text.view(spec->source), "Arguments");
}

TEST(CmdlineParserTest, IgnoresInitialWhitespaceOnlyLineForLocations) {
    constexpr auto bad = parse_spec<"    \n<bad_name>">();

    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().location.line, 1);
    EXPECT_EQ(bad.error().location.column, 5);
}

TEST(CmdlineParserTest, JoinSpecsNormalizesEachSpecBeforeConcatenating) {
    constexpr auto joined = join_specs<
        "\n        _Options\n          --config=\n",
        "\n            _Arguments\n              <input>\n">();

    constexpr auto spec = parse_spec<joined>();

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(joined.view(), "_Options\n  --config=\n_Arguments\n  <input>\n");
    EXPECT_EQ(spec->narg, 1);
    EXPECT_EQ(spec->nopt, 1);
    EXPECT_EQ(spec->lines[0].type, LineType::kHeading);
    EXPECT_EQ(spec->lines[0].text.view(spec->source), "Options");
    EXPECT_EQ(spec->lines[1].type, LineType::kOption);
    EXPECT_EQ(spec->opts[0].long_name.view(spec->source), "config");
    EXPECT_EQ(spec->lines[2].type, LineType::kHeading);
    EXPECT_EQ(spec->lines[2].text.view(spec->source), "Arguments");
    EXPECT_EQ(spec->args[0].name.view(spec->source), "input");
}

TEST(CmdlineParserTest, LineIndentFollowsLeadingWhitespace) {
    constexpr auto spec = parse_spec<R"(_Top
  _Second
    --flag
    Raw text)">();

    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->nline, 4);
    EXPECT_EQ(spec->lines[0].text.view(spec->source), "Top");
    EXPECT_EQ(spec->lines[0].indent, 0);
    EXPECT_EQ(spec->lines[1].text.view(spec->source), "Second");
    EXPECT_EQ(spec->lines[1].indent, 2);
    EXPECT_EQ(spec->lines[2].type, LineType::kOption);
    EXPECT_EQ(spec->lines[2].indent, 4);
    EXPECT_EQ(spec->lines[3].text.view(spec->source), "Raw text");
    EXPECT_EQ(spec->lines[3].indent, 4);
}

TEST(CmdlineCheckTest, ReportsCompileTimeValidationErrors) {
    constexpr auto bad = parse_and_check_spec<R"(
        _Arguments
          <paths:path>+
          <extra>
    )">();

    EXPECT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().error_message(), "variadic argument must be last");
}

TEST(CmdlineCheckTest, ReportsVariadicArgumentWhenNextArgIsParsed) {
    constexpr auto bad = parse_and_check_spec<R"(
        _Arguments
          <first>+
          <second>
          <third>
    )">();

    EXPECT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().error_message(), "variadic argument must be last");
}

TEST(CmdlineCheckTest, AllowsStarVariadicArgumentOnlyAtEnd) {
    constexpr auto good = parse_and_check_spec<R"(
        _Arguments
          <files>*
    )">();

    ASSERT_TRUE(good.has_value());

    constexpr auto parsed = parse_spec<R"(
        _Arguments
          <files>*
    )">();

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->args[0].variadic);
    EXPECT_FALSE(parsed->args[0].required);

    constexpr auto bad = parse_and_check_spec<R"(
        _Arguments
          <files>*
          <extra>
    )">();

    EXPECT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().error_message(), "variadic argument must be last");
}

TEST(CmdlineParserTest, ParsesOptionalPositionalArgument) {
    constexpr auto spec = parse_spec<R"(
        _Arguments
          <config>?
    )">();

    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->narg, 1);
    EXPECT_EQ(spec->args[0].name.view(spec->source), "config");
    EXPECT_FALSE(spec->args[0].variadic);
    EXPECT_FALSE(spec->args[0].required);
    EXPECT_TRUE(spec->args[0].optional());
}

TEST(CmdlineCheckTest, OptionalPositionalArgumentMustBeLast) {
    constexpr auto bad = parse_and_check_spec<R"(
        _Arguments
          <config>?
          <input>
    )">();

    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().error_message(), "optional argument must be last");
}

TEST(CmdlineCheckTest, AllowsCustomHelpOptionLine) {
    constexpr auto long_help = parse_and_check_spec<R"(
        _Options
          --help  Show help.
    )">();

    ASSERT_TRUE(long_help.has_value());

    constexpr auto long_short_help = parse_and_check_spec<R"(
        _Options
          --help/-h  Show help.
    )">();

    ASSERT_TRUE(long_short_help.has_value());

    constexpr auto short_help = parse_and_check_spec<R"(
        _Options
          -h  Show help.
    )">();

    ASSERT_TRUE(short_help.has_value());
}

TEST(CmdlineCheckTest, RejectsInvalidCustomHelpForms) {
    constexpr auto wrong_long = parse_and_check_spec<R"(
        _Options
          --host/-h  Host.
    )">();

    ASSERT_FALSE(wrong_long.has_value());
    EXPECT_EQ(wrong_long.error().error_message(), "reserved option name");

    constexpr auto wrong_short = parse_and_check_spec<R"(
        _Options
          --help/-x  Show help.
    )">();

    ASSERT_FALSE(wrong_short.has_value());
    EXPECT_EQ(wrong_short.error().error_message(), "reserved option name");

    constexpr auto value_help = parse_and_check_spec<R"(
        _Options
          --help=  Show help.
    )">();

    ASSERT_FALSE(value_help.has_value());
    EXPECT_EQ(value_help.error().error_message(), "reserved option name");

    constexpr auto repeat_help = parse_and_check_spec<R"(
        _Options
          --help+  Show help.
    )">();

    ASSERT_FALSE(repeat_help.has_value());
    EXPECT_EQ(repeat_help.error().error_message(), "reserved option name");

    constexpr auto hidden_help = parse_and_check_spec<R"(
        _Options
          --help  Show help. [hidden]
    )">();

    ASSERT_FALSE(hidden_help.has_value());
    EXPECT_EQ(hidden_help.error().error_message(), "reserved option name");
}

TEST(CmdlineCheckTest, RejectsDuplicateCustomHelpLines) {
    constexpr auto duplicate = parse_and_check_spec<R"(
        _Options
          --help  Show long help.
          -h  Show short help.
    )">();

    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().error_message(), "duplicate option name");
}

TEST(CmdlineCheckTest, RejectsValueTagsOnSwitches) {
    constexpr auto with_default = parse_and_check_spec<R"(
        _Options
          --dry-run  Validate only. [default: true]
    )">();

    ASSERT_FALSE(with_default.has_value());
    EXPECT_EQ(
        with_default.error().error_message(),
        "default tag requires a value-taking option");

    constexpr auto with_env = parse_and_check_spec<R"(
        _Options
          --dry-run  Validate only. [env: DRY_RUN]
    )">();

    ASSERT_FALSE(with_env.has_value());
    EXPECT_EQ(
        with_env.error().error_message(),
        "env tag requires a value-taking option");

    constexpr auto required = parse_and_check_spec<R"(
        _Options
          --dry-run  Validate only. [required]
    )">();

    ASSERT_FALSE(required.has_value());
    EXPECT_EQ(
        required.error().error_message(),
        "required tag requires a value-taking option");
}

TEST(CmdlineParserTest, RejectsAliasTags) {
    constexpr auto alias = parse_spec<R"(
        _Options
          --output  Output path. [alias: --out]
    )">();

    ASSERT_FALSE(alias.has_value());
    EXPECT_EQ(
        alias.error().error_message(), "option aliases are not supported");
}

TEST(CmdlineParserTest, RejectsUnknownOptionTags) {
    constexpr auto value = parse_spec<R"(
        _Options
          --output=  Output path. [unknown: value]
    )">();

    ASSERT_FALSE(value.has_value());
    EXPECT_EQ(value.error().error_message(), "unknown option tag");
    EXPECT_EQ(value.error().location.line, 2);
    EXPECT_EQ(value.error().location.column, 36);

    constexpr auto flag = parse_spec<R"(
        _Options
          --quiet  Quiet mode. [unknown]
    )">();

    ASSERT_FALSE(flag.has_value());
    EXPECT_EQ(flag.error().error_message(), "unknown option tag");
}

TEST(CmdlineParserTest, RejectsDuplicateOptionTags) {
    constexpr auto duplicate_default = parse_spec<R"(
        _Options
          --output=  Output path. [default: one] [default: two]
    )">();

    ASSERT_FALSE(duplicate_default.has_value());
    EXPECT_EQ(
        duplicate_default.error().error_message(), "duplicate option tag");
    EXPECT_EQ(duplicate_default.error().location.line, 2);
    EXPECT_EQ(duplicate_default.error().location.column, 51);

    constexpr auto duplicate_flag = parse_spec<R"(
        _Options
          --quiet  Quiet mode. [hidden] [hidden]
    )">();

    ASSERT_FALSE(duplicate_flag.has_value());
    EXPECT_EQ(duplicate_flag.error().error_message(), "duplicate option tag");
}

TEST(CmdlineParserTest, IdentifierNamesAreAlphanumericWithDashes) {
    constexpr auto spec = parse_spec<R"(
        _Arguments
          <1input:string>  Numeric start.

        _Options
          --2fast/-3       Numeric option names.
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->narg, 1);
    EXPECT_EQ(spec->nopt, 1);

    constexpr auto bad_arg = parse_spec<R"(
        _Arguments
          <bad_name>  Underscore is invalid.
    )">();
    EXPECT_FALSE(bad_arg.has_value());

    constexpr auto bad_option = parse_spec<R"(
        _Options
          --bad_name  Underscore is invalid.
    )">();
    EXPECT_FALSE(bad_option.has_value());
}

TEST(CmdlineParserTest, RejectsInvalidOptionNames) {
    constexpr auto short_long = parse_spec<R"(
        _Options
          --x  Too short.
    )">();

    ASSERT_FALSE(short_long.has_value());
    EXPECT_EQ(
        short_long.error().error_message(),
        "long option name must have at least two characters");

    constexpr auto trailing_dash = parse_spec<R"(
        _Options
          --output-  Trailing dash.
    )">();

    ASSERT_FALSE(trailing_dash.has_value());
    EXPECT_EQ(
        trailing_dash.error().error_message(), "name cannot end with '-'");

    constexpr auto consecutive_dash = parse_spec<R"(
        _Options
          --bad--name  Consecutive dash.
    )">();

    ASSERT_FALSE(consecutive_dash.has_value());
    EXPECT_EQ(
        consecutive_dash.error().error_message(),
        "name cannot contain consecutive '-'");
}

TEST(CmdlineParserTest, ReportsIdentifierErrorsAtInvalidCharacter) {
    constexpr auto bad_arg = parse_spec<"<bad_name>">();

    EXPECT_FALSE(bad_arg.has_value());
    EXPECT_EQ(bad_arg.error().location.line, 1);
    EXPECT_EQ(bad_arg.error().location.column, 5);

    constexpr auto bad_option = parse_spec<"--bad_name">();

    EXPECT_FALSE(bad_option.has_value());
    EXPECT_EQ(bad_option.error().location.line, 1);
    EXPECT_EQ(bad_option.error().location.column, 6);
}

TEST(CmdlineParserTest, AcceptsPlusRepeatOnSwitchesAsCountSwitches) {
    constexpr auto spec = parse_spec<R"(
        _Options
          --verbose/-v+
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_TRUE(spec->opts[0].counting);
}

TEST(CmdlineParserTest, RejectsAdjacentRepeatMarkers) {
    constexpr auto positional = parse_spec<R"(
        _Arguments
          <files>+*
    )">();

    ASSERT_FALSE(positional.has_value());
    EXPECT_EQ(positional.error().error_message(), "duplicate repeat marker");

    constexpr auto optional = parse_spec<R"(
        _Arguments
          <config>?+
    )">();

    ASSERT_FALSE(optional.has_value());
    EXPECT_EQ(optional.error().error_message(), "duplicate repeat marker");

    constexpr auto plus = parse_spec<R"(
        _Options
          --verbose++
    )">();

    ASSERT_FALSE(plus.has_value());
    EXPECT_EQ(plus.error().error_message(), "duplicate repeat marker");

    constexpr auto last_first = parse_spec<R"(
        _Options
          --cache=<path>><
    )">();

    ASSERT_FALSE(last_first.has_value());
    EXPECT_EQ(last_first.error().error_message(), "duplicate repeat marker");
}

TEST(CmdlineParserTest, AcceptsRepeatMarkerInDescriptionAfterWhitespace) {
    constexpr auto spec = parse_spec<R"(
        _Options
          --verbose+ + increases logging.
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->opts[0].repeat, RepeatPolicy::kAll);
    EXPECT_EQ(
        spec->opts[0].description.view(spec->source), "+ increases logging.");
}

TEST(CmdlineParserTest, AcceptsShortOnlyOption) {
    constexpr auto spec = parse_spec<R"(
        _Options
          -q  Quiet mode.
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_TRUE(spec->opts[0].long_name.empty());
    EXPECT_EQ(spec->opts[0].short_name.view(spec->source), "q");
}

TEST(CmdlineParserTest, AcceptsFirstAndLastShortOnlySwitches) {
    constexpr auto spec = parse_spec<R"(
        _Options
          -q<  First quiet wins.
          -v>  Last verbose wins.
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->opts[0].repeat, RepeatPolicy::kFirst);
    EXPECT_EQ(spec->opts[1].repeat, RepeatPolicy::kLast);
}

TEST(CmdlineParserTest, RejectsShortLongOptionPair) {
    constexpr auto bad = parse_spec<R"(
        _Options
          -q/--quiet  Quiet mode.
    )">();

    EXPECT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().error_message(), "expected option delimiter");
}

TEST(CmdlineParserTest, RejectsSecondLongOptionName) {
    constexpr auto bad = parse_spec<R"(
        _Options
          --quiet/--silent  Quiet mode.
    )">();

    EXPECT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().error_message(), "expected short option");
}

TEST(CmdlineParserTest, RequiresValueGrammarImmediatelyAfterEquals) {
    constexpr auto bare_space = parse_spec<R"(
        _Options
          --switch <path>  Description.
    )">();

    ASSERT_TRUE(bare_space.has_value());
    EXPECT_FALSE(bare_space->opts[0].takes_value);
    EXPECT_EQ(
        bare_space->opts[0].description.view(bare_space->source),
        "<path>  Description.");

    constexpr auto spaced_type = parse_spec<R"(
        _Options
          --output= <path>  Output path.
    )">();

    ASSERT_TRUE(spaced_type.has_value());
    EXPECT_TRUE(spaced_type->opts[0].takes_value);
    EXPECT_EQ(spaced_type->opts[0].value_type, ValueType::kString);
    EXPECT_FALSE(spaced_type->opts[0].value_name);
    EXPECT_EQ(
        spaced_type->opts[0].description.view(spaced_type->source),
        "<path>  Output path.");

    constexpr auto escaped_choice_text = parse_spec<R"(
        _Options
          --channel= \[dev|prod\]  Release channel.
    )">();

    ASSERT_TRUE(escaped_choice_text.has_value());
    EXPECT_TRUE(escaped_choice_text->opts[0].takes_value);
    EXPECT_EQ(escaped_choice_text->opts[0].value_type, ValueType::kString);
    EXPECT_FALSE(escaped_choice_text->opts[0].choices);
    EXPECT_EQ(
        escaped_choice_text->opts[0].description.view(
            escaped_choice_text->source),
        R"(\[dev|prod\]  Release channel.)");

    constexpr auto unescaped_choice_text = parse_spec<R"(
        _Options
          --channel= [dev|prod]  Release channel.
    )">();

    ASSERT_FALSE(unescaped_choice_text.has_value());
    EXPECT_EQ(
        unescaped_choice_text.error().error_message(), "unknown option tag");
}

TEST(CmdlineParserTest, RejectsEmptyChoiceValues) {
    constexpr auto empty = parse_spec<R"(
        _Options
          --channel=[]  Empty choice list.
    )">();

    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().error_message(), "choice value cannot be empty");

    constexpr auto double_bar = parse_spec<R"(
        _Options
          --channel=[dev||prod]  Empty middle choice.
    )">();

    ASSERT_FALSE(double_bar.has_value());
    EXPECT_EQ(
        double_bar.error().error_message(), "choice value cannot be empty");

    constexpr auto trailing_bar = parse_spec<R"(
        _Options
          --channel=[dev|]  Empty trailing choice.
    )">();

    ASSERT_FALSE(trailing_bar.has_value());
    EXPECT_EQ(
        trailing_bar.error().error_message(), "choice value cannot be empty");
}

TEST(CmdlineParserTest, RejectsNonIdentifierChoiceValues) {
    constexpr auto spaces = parse_spec<R"(
        _Options
          --channel=[dev |prod]  Spaced choice.
    )">();

    ASSERT_FALSE(spaces.has_value());
    EXPECT_EQ(
        spaces.error().error_message(), "choice value must be an identifier");

    constexpr auto escape = parse_spec<R"(
        _Options
          --channel=[dev\|prod]  Escaped separator.
    )">();

    ASSERT_FALSE(escape.has_value());
    EXPECT_EQ(
        escape.error().error_message(), "choice value must be an identifier");
}

TEST(CmdlineParserTest, RejectsEmptyValueTags) {
    constexpr auto empty_default = parse_spec<R"(
        _Options
          --output=  Output path. [default:]
    )">();

    ASSERT_FALSE(empty_default.has_value());
    EXPECT_EQ(
        empty_default.error().error_message(),
        "default tag value cannot be empty");

    constexpr auto empty_env = parse_spec<R"(
        _Options
          --output=  Output path. [env:   ]
    )">();

    ASSERT_FALSE(empty_env.has_value());
    EXPECT_EQ(
        empty_env.error().error_message(), "env tag value cannot be empty");
}

TEST(CmdlineParserTest, ValidatesEnvironmentVariableNames) {
    constexpr auto spec = parse_spec<R"(
        _Options
          --output=  Output path. [env: _OUTPUT_PATH_1]
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->opts[0].env_name.view(spec->source), "_OUTPUT_PATH_1");

    constexpr auto space = parse_spec<R"(
        _Options
          --output=  Output path. [env: OUTPUT PATH]
    )">();

    ASSERT_FALSE(space.has_value());
    EXPECT_EQ(
        space.error().error_message(),
        "environment variable name must start with alpha or '_' and contain "
        "only alphanumeric characters or '_'");
    EXPECT_EQ(space.error().location.line, 2);
    EXPECT_EQ(space.error().location.column, 47);

    constexpr auto dash = parse_spec<R"(
        _Options
          --output=  Output path. [env: OUTPUT-PATH]
    )">();

    ASSERT_FALSE(dash.has_value());
    EXPECT_EQ(
        dash.error().error_message(),
        "environment variable name must start with alpha or '_' and contain "
        "only alphanumeric characters or '_'");

    constexpr auto digit = parse_spec<R"(
        _Options
          --output=  Output path. [env: 1_OUTPUT_PATH]
    )">();

    ASSERT_FALSE(digit.has_value());
    EXPECT_EQ(
        digit.error().error_message(),
        "environment variable name must start with alpha or '_' and contain "
        "only alphanumeric characters or '_'");
}

TEST(CmdlineParserTest, RequiresQuotesForDefaultWhitespace) {
    constexpr auto quoted = parse_spec<R"(
        _Options
          --output=  Output path. [default: "two words"]
    )">();

    ASSERT_TRUE(quoted.has_value());
    EXPECT_EQ(quoted->opts[0].default_value.view(quoted->source), "two words");

    constexpr auto unquoted = parse_spec<R"(
        _Options
          --output=  Output path. [default: two words]
    )">();

    ASSERT_FALSE(unquoted.has_value());
    EXPECT_EQ(unquoted.error().error_message(), "misquoted default value");
    EXPECT_EQ(unquoted.error().location.line, 2);
    EXPECT_EQ(unquoted.error().location.column, 48);
}

TEST(CmdlineParserTest, QuotedDefaultsCanEscapeQuotes) {
    constexpr auto quoted = parse_spec<R"(
        _Options
          --name=  Name. [default: "say \"hello\""]
    )">();

    ASSERT_TRUE(quoted.has_value());
    EXPECT_EQ(
        quoted->opts[0].default_value.view(quoted->source), R"(say \"hello\")");
}

TEST(CmdlineParserTest, QuotedDefaultsCanEscapeBackslashes) {
    constexpr auto quoted = parse_spec<R"(
        _Options
          --path=  Path. [default: "C:\\Temp"]
    )">();

    ASSERT_TRUE(quoted.has_value());
    EXPECT_TRUE(quoted->opts[0].default_quoted);
    EXPECT_EQ(
        quoted->opts[0].default_value.view(quoted->source), R"(C:\\Temp)");

    constexpr auto bad_escape = parse_spec<R"(
        _Options
          --path=  Path. [default: "C:\Temp"]
    )">();

    ASSERT_FALSE(bad_escape.has_value());
    EXPECT_EQ(bad_escape.error().error_message(), "misquoted default value");
}

TEST(CmdlineParserTest, QuotedDefaultsDoNotEscapeClosingBracket) {
    constexpr auto quoted = parse_spec<R"(
        _Options
          --name=  Name. [default: "say ] hello"]
    )">();

    ASSERT_FALSE(quoted.has_value());
    EXPECT_EQ(quoted.error().error_message(), "misquoted default value");
}

TEST(CmdlineParserTest, RequiresTagsAtEndOfDescription) {
    constexpr auto spec = parse_spec<R"(
        _Options
          --output=  Output path. [env: OUTPUT] [default: file]
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->opts[0].description.view(spec->source), "Output path.");

    constexpr auto trailing_text = parse_spec<R"(
        _Options
          --output=  Output path. [env: OUTPUT] trailing text.
    )">();

    ASSERT_FALSE(trailing_text.has_value());
    EXPECT_EQ(
        trailing_text.error().error_message(),
        "option tags must come after description");
}

TEST(CmdlineParserTest, EscapedDescriptionBracketsDoNotStartTags) {
    constexpr auto spec = parse_spec<R"(
        _Options
          --output=  Write \[file\] path. [env: OUTPUT]
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(
        spec->opts[0].description.view(spec->source),
        R"(Write \[file\] path.)");
    EXPECT_EQ(spec->opts[0].env_name.view(spec->source), "OUTPUT");
}

TEST(CmdlineCheckTest, RejectsChoiceDefaultsOutsideChoices) {
    constexpr auto scalar = parse_and_check_spec<R"(
        _Options
          --channel=[dev|prod]  Release channel. [default: stage]
    )">();

    ASSERT_FALSE(scalar.has_value());
    EXPECT_EQ(scalar.error().error_message(), "invalid default value");

    constexpr auto repeat = parse_and_check_spec<R"(
        _Options
          --channel=[dev|prod]+  Release channel. [default: dev,bad]
    )">();

    ASSERT_FALSE(repeat.has_value());
    EXPECT_EQ(repeat.error().error_message(), "invalid default value");

    constexpr auto comma_scalar = parse_and_check_spec<R"(
        _Options
          --channel=[dev|prod]  Release channel. [default: dev,prod]
    )">();

    ASSERT_FALSE(comma_scalar.has_value());
    EXPECT_EQ(comma_scalar.error().error_message(), "invalid default value");
}

TEST(CmdlineCheckTest, RejectsImplicitNoOptionNameDuplicates) {
    constexpr auto explicit_no = parse_and_check_spec<R"(
        _Options
          --feature>     Toggleable feature.
          --no-feature   Explicit negative form.
    )">();

    ASSERT_FALSE(explicit_no.has_value());
    EXPECT_EQ(explicit_no.error().error_message(), "duplicate option name");

    constexpr auto implicit_no = parse_and_check_spec<R"(
        _Options
          --no-feature   Explicit negative form.
          --feature>     Toggleable feature.
    )">();

    ASSERT_FALSE(implicit_no.has_value());
    EXPECT_EQ(implicit_no.error().error_message(), "duplicate option name");
}

TEST(CmdlineParserTest, KeepsStringOptionDescriptionAfterEquals) {
    constexpr auto spec = parse_spec<R"(
        _Options
          --token=  Access token.
    )">();

    ASSERT_TRUE(spec.has_value());
    EXPECT_TRUE(spec->opts[0].takes_value);
    EXPECT_EQ(spec->opts[0].value_type, ValueType::kString);
    EXPECT_EQ(spec->opts[0].description.view(spec->source), "Access token.");
}

}  // namespace

}  // namespace cmd
