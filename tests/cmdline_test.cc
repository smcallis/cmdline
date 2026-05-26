#include "cmdline.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "cmdline_test_helpers.h"

namespace cmd {

namespace {

constexpr char kSpec[] = R"(
    sync files.

    _Arguments
      <source:path>       Source path.
      <destination:path>  Destination path.
      <include:path>+     Extra include paths.

    _Options
      --config/-c=<path>        Config file.
      --format/-f=[json|yaml]   Output format. [default: json]
      --tag/-t=<name>+          Repeatable tags.
      --dry-run                 Validate without copying.
      --quiet                   Quiet flag. [hidden]
      --feature>                Toggleable feature.
      --cache=<path>>           Last cache path wins.
      --first=<name><           First profile wins.
      --token=                  Access token. [required]
      --verbose/-v+             Increase verbosity.
      --old-token=              Old token.
)";

constexpr char kOverrideSpec[] = R"(
    _Arguments
      <input>

    _Options
      --name=<widget>
)";

// Custom type used to prove earlier non-default types can be overridden.
struct FirstWidgetType {
    using type = std::string;

    static constexpr std::string_view name = "widget";

    // Prefixes parsed text with the first widget type marker.
    static std::expected<type, TypeConversionError> parse(
        std::string_view text) {
        return "first-widget:" + std::string(text);
    }
};

// Custom type expected to override the earlier widget type.
struct LastWidgetType {
    using type = std::string;

    static constexpr std::string_view name = "widget";

    // Prefixes parsed text with the last widget type marker.
    static std::expected<type, TypeConversionError> parse(
        std::string_view text) {
        return "last-widget:" + std::string(text);
    }
};

// Custom type expected to override the built-in string type.
struct OverrideStringType {
    using type = std::string;

    static constexpr std::string_view name = "string";

    // Prefixes parsed text to prove the built-in string type was not selected.
    static std::expected<type, TypeConversionError> parse(
        std::string_view text) {
        return "custom-string:" + std::string(text);
    }
};

// Custom type whose parser can be used for compile-time default parsing.
struct PortType {
    using type = int;

    static constexpr std::string_view name = "port";

    // Parses a decimal port number.
    static constexpr std::expected<type, TypeConversionError> parse(
        std::string_view text) {
        if (text.empty()) {
            return std::unexpected(TypeConversionError("empty port"));
        }

        type value = 0;
        for (char ch : text) {
            if (ch < '0' || ch > '9') {
                return std::unexpected(TypeConversionError("invalid port"));
            }
            value = value * 10 + (ch - '0');
        }
        return value;
    }
};

// Custom type that validates at runtime but not during spec compilation.
struct RuntimePortType {
    using type = int;

    static constexpr std::string_view name = "runtime-port";

    // Parses a decimal port number.
    static std::expected<type, TypeConversionError> parse(
        std::string_view text) {
        return PortType::parse(text);
    }
};

// Custom type that only accepts one comma-containing scalar value.
struct CommaType {
    using type = int;

    static constexpr std::string_view name = "comma";

    // Parses exactly "a,b" to prove scalar defaults are not comma-split.
    static constexpr std::expected<type, TypeConversionError> parse(
        std::string_view text) {
        if (text != "a,b") {
            return std::unexpected(TypeConversionError("invalid comma value"));
        }
        return 1;
    }
};

// Custom type that accepts a quote-containing default at compile time.
struct QuoteDefaultType {
    using type = bool;

    static constexpr std::string_view name = "quote-default";

    // Parses one value that includes an embedded quote.
    static constexpr std::expected<type, TypeConversionError> parse(
        std::string_view text) {
        if (text != R"(say "hello")") {
            return std::unexpected(TypeConversionError("invalid quote value"));
        }
        return true;
    }
};

using cmd_test::SavedColorEnv;
using cmd_test::SavedEnv;
using cmd_test::strip_ansi;

template <typename Parsed>
std::string capture_usage(const Parsed& parsed) {
    FILE* file = std::tmpfile();
    EXPECT_NE(file, nullptr);
    if (file == nullptr) {
        return {};
    }

    parsed.usage(file);
    std::fflush(file);
    std::fseek(file, 0, SEEK_SET);

    std::string output;
    std::array<char, 4096> buffer{};
    while (size_t count = std::fread(buffer.data(), 1, buffer.size(), file)) {
        output.append(buffer.data(), count);
    }

    std::fclose(file);
    return output;
}

template <FixedString kSpecText, SpecTypeList... Lists>
std::string rendered_usage(
    std::string_view program, std::span<const UsageFace> faces = {}) {
    std::string program_text(program);
    const char* argv[] = {program_text.c_str(), "--help"};

    auto parsed = try_parse_cmdline<kSpecText, Lists...>(2, argv, faces);
    EXPECT_TRUE(parsed) << cmdline_error_message(parsed.error());
    if (!parsed) {
        return {};
    }

    return capture_usage(*parsed);
}

template <FixedString kSpecText, SpecTypeList... Lists>
auto runtime_cmdline(std::span<const UsageFace> faces = {}) {
    const char* argv[] = {"tool"};
    return Cmdline<kSpecText, Lists...>::help(1, argv, faces);
}

TEST(CmdlineTest, ParserAcceptsCanonicalSpec) {
    auto parsed      = runtime_cmdline<kSpec>();
    std::string help = strip_ansi(rendered_usage<kSpec>("sync"));

    EXPECT_EQ(parsed.args().size(), 3);
    EXPECT_EQ(parsed.opts().size(), 11);
    EXPECT_NE(help.find("Arguments\n"), std::string::npos);
    EXPECT_EQ(help.find("_Arguments"), std::string::npos);
    EXPECT_NE(help.find("[--help/-h]"), std::string::npos);
    EXPECT_EQ(parsed.args()[0].name, "source");
    EXPECT_EQ(parsed.args()[0].type_name, "string");
    EXPECT_EQ(parsed.args()[0].face_name, "path");
    EXPECT_TRUE(parsed.args()[2].accepts_many);
    EXPECT_TRUE(parsed.opts()[4].hidden);
    EXPECT_TRUE(parsed.opts()[8].required);
    EXPECT_TRUE(parsed.opts()[9].count_occurrences);
}

TEST(CmdlineTest, JoinedSpecsWorkThroughPublicEntryPoint) {
    constexpr auto joined = join_specs<
        "\n    _Options\n      --token=\n",
        "\n        _Arguments\n          <input>\n">();

    const char* argv[] = {"tool", "--token", "secret", "file"};
    auto parsed        = try_parse_cmdline<joined>(4, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->arg<"input">(), "file");
    ASSERT_TRUE(parsed->opt<"--token">());
    EXPECT_EQ(*parsed->opt<"--token">(), "secret");
}

TEST(CmdlineTest, UsageFormatsExpectedText) {
    std::string plain = strip_ansi(rendered_usage<kSpec>("sync"));

    EXPECT_NE(plain.find("--config/-c=<path>"), std::string::npos);
    EXPECT_NE(plain.find("--tag/-t=<name>"), std::string::npos);
    EXPECT_EQ(plain.find("--tag/-t=<name>+"), std::string::npos);
    EXPECT_NE(plain.find("--token="), std::string::npos);
    EXPECT_NE(plain.find("--help/-h"), std::string::npos);
    EXPECT_NE(plain.find("Print this message and exit."), std::string::npos);
    EXPECT_EQ(plain.find("<source:path>"), std::string::npos);
    EXPECT_NE(plain.find("--[no-]feature"), std::string::npos);
    EXPECT_EQ(plain.find("--feature  "), std::string::npos);
    EXPECT_EQ(plain.find("--feature>"), std::string::npos);
    EXPECT_TRUE(plain.starts_with("\n │  Usage: sync"));
    EXPECT_TRUE(plain.ends_with("\n"));
    EXPECT_FALSE(plain.ends_with(" │\n"));
    EXPECT_NE(plain.find(" │  Usage: sync"), std::string::npos);
    EXPECT_EQ(plain.find("+│  --tag/-t=<name>"), std::string::npos);
    EXPECT_EQ(plain.find(">│  --[no-]feature"), std::string::npos);
    EXPECT_NE(plain.find("+│ Repeatable tags."), std::string::npos);
    EXPECT_NE(plain.find(">│ Toggleable feature."), std::string::npos);
    EXPECT_NE(plain.find("│ Output format."), std::string::npos);
}

TEST(CmdlineTest, UsageUsesCustomHelpLineWhenDeclared) {
    std::string long_plain = strip_ansi(rendered_usage<R"(
        _Options
          --help  Show custom help.
          --dry-run  Validate only.
    )">("tool"));

    EXPECT_NE(long_plain.find("--help"), std::string::npos);
    EXPECT_NE(long_plain.find("Show custom help."), std::string::npos);
    EXPECT_EQ(
        long_plain.find("Print this message and exit."), std::string::npos);

    std::string long_short_plain = strip_ansi(rendered_usage<R"(
        _Options
          --help/-h  Show custom help.
          --dry-run  Validate only.
    )">("tool"));

    EXPECT_NE(long_short_plain.find("--help/-h"), std::string::npos);
    EXPECT_NE(long_short_plain.find("Show custom help."), std::string::npos);
    EXPECT_EQ(
        long_short_plain.find("Print this message and exit."),
        std::string::npos);

    std::string short_plain = strip_ansi(rendered_usage<R"(
        _Options
          -h  Show custom help.
          --dry-run  Validate only.
    )">("tool"));

    EXPECT_NE(short_plain.find("-h"), std::string::npos);
    EXPECT_NE(short_plain.find("Show custom help."), std::string::npos);
    EXPECT_EQ(
        short_plain.find("Print this message and exit."), std::string::npos);
}

TEST(CmdlineTest, UsagePreservesManualIndentation) {
    std::string plain = strip_ansi(rendered_usage<R"(
        _Top
          _Second
            --flag  Flag.
        _Back
        --root  Root.
    )">("tool"));
    EXPECT_NE(plain.find("\n │  Top\n"), std::string::npos);
    EXPECT_NE(plain.find("\n │    Second\n"), std::string::npos);
    EXPECT_NE(plain.find("\n │      --flag"), std::string::npos);
    EXPECT_NE(plain.find("\n │  Back\n"), std::string::npos);
    EXPECT_NE(plain.find("\n │  --root"), std::string::npos);
}

TEST(CmdlineTest, UsageUnescapesDescriptionBrackets) {
    std::string plain = strip_ansi(rendered_usage<R"(
        _Options
          --output=  Write \[file\] path. [env: OUTPUT]
    )">("tool"));
    EXPECT_NE(plain.find("Write [file] path."), std::string::npos);
    EXPECT_EQ(plain.find(R"(\[file\])"), std::string::npos);
}

TEST(CmdlineTest, UsagePreservesOptionOrder) {
    std::string plain = strip_ansi(rendered_usage<R"(
        _First
          --zulu  Zulu.
          --alpha  Alpha.

        _Second
          --beta  Beta.
          --aardvark  Aardvark.
    )">("tool"));
    size_t alpha      = plain.find("--alpha");
    size_t zulu       = plain.find("--zulu");
    size_t second     = plain.find("Second");
    size_t aardvark   = plain.find("--aardvark");
    size_t beta       = plain.find("--beta");
    ASSERT_NE(alpha, std::string::npos);
    ASSERT_NE(zulu, std::string::npos);
    ASSERT_NE(second, std::string::npos);
    ASSERT_NE(aardvark, std::string::npos);
    ASSERT_NE(beta, std::string::npos);
    EXPECT_LT(zulu, alpha);
    EXPECT_LT(zulu, second);
    EXPECT_LT(beta, aardvark);
}

TEST(CmdlineTest, ColorPolicyHonorsEnvironment) {
    FILE* file = std::tmpfile();
    ASSERT_NE(file, nullptr);

    SavedColorEnv env;
    env.clear();
    EXPECT_FALSE(color_enabled(file));

    env.force_color_output();
    EXPECT_TRUE(color_enabled(file));

    setenv("NO_COLOR", "1", 1);
    EXPECT_FALSE(color_enabled(file));

    std::fclose(file);
}

TEST(CmdlineTest, ParserStripsCommonLeadingWhitespace) {
    auto parsed = runtime_cmdline<R"(
        _Arguments
          <input:path>  Input.

        _Options
          --dry-run     Validate only.
    )">();

    ASSERT_EQ(parsed.args().size(), 1);
    EXPECT_EQ(parsed.args()[0].name, "input");
    ASSERT_EQ(parsed.opts().size(), 1);
    EXPECT_EQ(parsed.opts()[0].long_name, "dry-run");
}

TEST(CmdlineTest, MatchesShortSwitchBlocksAndRepeatOptions) {
    const char* argv[] = {"sync", "--token=secret", "--quiet", "-vvv", "--tag",
                          "one",  "--tag=two",      "src",     "dst",  "inc1",
                          "inc2"};

    auto parsed = try_parse_cmdline<kSpec>(11, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_TRUE(parsed->opt<"--quiet">());
    static_assert(std::is_same_v<decltype(parsed->opt<"-v">()), const int&>);
    EXPECT_EQ(parsed->opt<"-v">(), 3);
    std::optional<std::string> token = parsed->opt<"--token">();
    ASSERT_TRUE(token);
    EXPECT_EQ(*token, "secret");
    EXPECT_EQ(parsed->opt<"--tag">(), (std::vector<std::string>{"one", "two"}));
    EXPECT_EQ(parsed->opt<"-t">(), (std::vector<std::string>{"one", "two"}));
    EXPECT_EQ(parsed->arg<"source">(), "src");
    EXPECT_EQ(parsed->arg<"destination">(), "dst");
    EXPECT_EQ(
        parsed->arg<"include">(), (std::vector<std::string>{"inc1", "inc2"}));
}

TEST(CmdlineTest, RejectsShortBlocksWithValueOptions) {
    const char* argv[] = {"tool", "-vo", "file"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          -v   Verbose.
          -o=  Output.
    )">(3, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kUnknown);
    EXPECT_EQ(cmdline_error_message(parsed.error()), "unknown option: -vo");
}

TEST(CmdlineTest, AccessorPrefixesSelectOnlyMatchingNameType) {
    const char* argv[] = {"tool", "-vv"};

    using Spec = TypedSpec<checked_spec<kSpec>()>;
    static_assert(Spec::is_help_name("--help"));
    static_assert(Spec::is_help_name("-h"));
    static_assert(!Spec::is_help_name("help"));
    static_assert(!Spec::is_help_name("h"));

    auto parsed = try_parse_cmdline<R"(
        _Options
          -v+  Verbose.
    )">(2, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->opt<"-v">(), 2);
}

TEST(CmdlineTest, AppliesDefaultsAndChoices) {
    const char* argv[] = {"sync", "--token", "secret", "src", "dst", "inc"};

    auto parsed = try_parse_cmdline<kSpec>(6, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->opt<"--format">(), "json");
}

TEST(CmdlineTest, ScalarDefaultsKeepCommas) {
    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --name=  Name. [default: a,b]
    )">(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->opt<"--name">(), "a,b");
}

TEST(CmdlineTest, ScalarStringAccessorsDistinguishMissingFromEmpty) {
    const char* argv[] = {"tool", "--name="};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --name=  Name.
    )">(2, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> value = parsed->opt<"--name">();
    ASSERT_TRUE(value);
    EXPECT_TRUE(value->empty());

    const char* empty_argv[] = {"tool"};
    auto missing             = try_parse_cmdline<R"(
        _Options
          --name=  Name.
    )">(1, empty_argv);

    ASSERT_TRUE(missing) << cmdline_error_message(missing.error());
    EXPECT_FALSE(missing->opt<"--name">());
}

TEST(CmdlineTest, EmptyEnvironmentValueIsPresent) {
    SavedEnv saved("CMDLINE_EMPTY_NAME");
    setenv("CMDLINE_EMPTY_NAME", "", 1);

    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --name=  Name. [env: CMDLINE_EMPTY_NAME]
    )">(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> value = parsed->opt<"--name">();
    ASSERT_TRUE(value);
    EXPECT_TRUE(value->empty());
}

TEST(CmdlineTest, ScalarTypedDefaultsKeepCommas) {
    const char* argv[] = {"tool"};

    using Types = TypeList<CommaType>;

    auto parsed = try_parse_cmdline<
        R"(
        _Options
          --pair=<comma>  Pair. [default: a,b]
    )",
        Types>(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->opt<"--pair">(), 1);
}

TEST(CmdlineTest, RepeatDefaultsSplitCommas) {
    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --tag=<name>+  Tags. [default: a,b,c\path,d\\path]
    )">(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(
        parsed->opt<"--tag">(),
        (std::vector<std::string>{"a", "b", R"(c\path)", R"(d\\path)"}));
}

TEST(CmdlineTest, RepeatEnvValuesSplitCommas) {
    SavedEnv saved("CMDLINE_TAGS");
    setenv("CMDLINE_TAGS", "a,b,c", 1);

    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --tag=<name>+  Tags. [env: CMDLINE_TAGS]
    )">(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(
        parsed->opt<"--tag">(), (std::vector<std::string>{"a", "b", "c"}));
}

TEST(CmdlineTest, TypedDefaultsCanBeValidatedAtCompileTime) {
    const char* argv[] = {"server"};

    using Types = TypeList<PortType>;

    auto parsed = try_parse_cmdline<
        R"(
        _Options
          --port=<port>  Listen port. [default: 443]
    )",
        Types>(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->opt<"--port">(), 443);

    auto repeat = try_parse_cmdline<
        R"(
        _Options
          --port=<port>+  Listen ports. [default: 80,443]
    )",
        Types>(1, argv);

    ASSERT_TRUE(repeat) << cmdline_error_message(repeat.error());
    EXPECT_EQ(repeat->opt<"--port">(), (std::vector<int>{80, 443}));
}

TEST(CmdlineTest, BuiltInNumericTypesParseValuesAndDefaults) {
    const char* argv[] = {"tool", "--count", "-42", "--ratio", "0.25"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --count=<int>       Signed value to use.
          --limit=<uint>      Unsigned limit. [default: 1024]
          --ratio=<float>     Ratio to use.
          --scale=<double>    Scale factor. [default: 1.5]
          --offsets=<int>+    Signed offsets. [default: -1,0,42]
          --sizes=<uint>+     Unsigned sizes. [default: 8,16]
    )">(5, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    const std::optional<int64_t>& count = parsed->opt<"--count">();
    ASSERT_TRUE(count);
    EXPECT_EQ(*count, -42);
    EXPECT_EQ(parsed->opt<"--limit">(), 1024);

    const std::optional<float>& ratio = parsed->opt<"--ratio">();
    ASSERT_TRUE(ratio);
    EXPECT_FLOAT_EQ(*ratio, 0.25f);
    EXPECT_DOUBLE_EQ(parsed->opt<"--scale">(), 1.5);

    EXPECT_EQ(parsed->opt<"--offsets">(), (std::vector<int64_t>{-1, 0, 42}));
    EXPECT_EQ(parsed->opt<"--sizes">(), (std::vector<uint64_t>{8, 16}));
}

TEST(CmdlineTest, BuiltInFloatingTypesRejectInvalidValues) {
    const char* argv[] = {"tool", "--ratio", "bad"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --ratio=<double>  Ratio to use.
    )">(3, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(
        cmdline_error_message(parsed.error()),
        "'bad' is not a valid double for --ratio: invalid double");
}

TEST(CmdlineTest, BuiltInNumericTypesRejectInvalidValues) {
    const char* int_argv[] = {"tool", "--count", "bad"};

    auto invalid_int = try_parse_cmdline<R"(
        _Options
          --count=<int>  Signed value to use.
    )">(3, int_argv);

    ASSERT_FALSE(invalid_int);
    EXPECT_EQ(
        cmdline_error_message(invalid_int.error()),
        "'bad' is not a valid int for --count: invalid int");

    const char* uint_argv[] = {"tool", "--limit", "-1"};

    auto invalid_uint = try_parse_cmdline<R"(
        _Options
          --limit=<uint>  Unsigned value to use.
    )">(3, uint_argv);

    ASSERT_FALSE(invalid_uint);
    EXPECT_EQ(
        cmdline_error_message(invalid_uint.error()),
        "'-1' is not a valid uint for --limit: invalid uint");

    const char* float_argv[] = {"tool", "--ratio", "bad"};

    auto invalid_float = try_parse_cmdline<R"(
        _Options
          --ratio=<float>  Ratio to use.
    )">(3, float_argv);

    ASSERT_FALSE(invalid_float);
    EXPECT_EQ(
        cmdline_error_message(invalid_float.error()),
        "'bad' is not a valid float for --ratio: invalid float");
}

TEST(CmdlineTest, TryParseCmdlineRejectsInvalidTypedValues) {
    const char* argv[] = {"server", "--port", "bad"};

    using Types = TypeList<PortType>;

    auto parsed = try_parse_cmdline<
        R"(
        _Options
          --port=<port>  Listen port.
    )",
        Types>(3, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kBadArgument);
    EXPECT_EQ(
        cmdline_error_message(parsed.error()),
        "'bad' is not a valid port for --port: invalid port");
    EXPECT_TRUE(parsed.error().location.has_location);
    EXPECT_EQ(parsed.error().location.argument_index, 2);

    const char* positional_argv[] = {"server", "bad"};

    auto positional = try_parse_cmdline<
        R"(
        _Arguments
          <port:port>  Listen port.
    )",
        Types>(2, positional_argv);

    ASSERT_FALSE(positional);
    EXPECT_EQ(positional.error().code, ErrorCode::kBadArgument);
    EXPECT_EQ(
        cmdline_error_message(positional.error()),
        "'bad' is not a valid port for port: invalid port");
    EXPECT_TRUE(positional.error().location.has_location);
    EXPECT_EQ(positional.error().location.argument_index, 1);
}

TEST(CmdlineTest, TryParseCmdlineRejectsInvalidTypedVariadicPositionals) {
    const char* argv[] = {"server", "80", "bad"};

    using Types = TypeList<PortType>;

    auto parsed = try_parse_cmdline<
        R"(
        _Arguments
          <ports:port>+  Listen ports.
    )",
        Types>(3, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kBadArgument);
    EXPECT_EQ(
        cmdline_error_message(parsed.error()),
        "'bad' is not a valid port for ports: invalid port");
    EXPECT_TRUE(parsed.error().location.has_location);
    EXPECT_EQ(parsed.error().location.argument_index, 2);
}

TEST(CmdlineTest, TryParseCmdlineReportsEnvAndDefaultTypedValueContext) {
    SavedEnv saved("CMDLINE_PORT");
    setenv("CMDLINE_PORT", "bad", 1);

    const char* argv[] = {"server"};

    using EnvTypes = TypeList<PortType>;

    auto env = try_parse_cmdline<
        R"(
        _Options
          --port=<port>  Listen port. [env: CMDLINE_PORT]
    )",
        EnvTypes>(1, argv);

    ASSERT_TRUE(env) << cmdline_error_message(env.error());
    std::optional<int> port = env->opt<"--port">();
    EXPECT_FALSE(port);
    ASSERT_EQ(env->warnings().size(), 1);
    EXPECT_EQ(
        cmdline_error_message(env->warnings()[0]),
        "'bad' is not a valid port for environment variable CMDLINE_PORT: "
        "invalid port");
    EXPECT_FALSE(env->warnings()[0].location.has_location);
    std::string env_diagnostic = format_cmdline_warning(
        env->warnings()[0], default_syntax_faces(), false);
    EXPECT_NE(
        env_diagnostic.find("warning: ignoring invalid environment variable"),
        std::string::npos);
    EXPECT_NE(env_diagnostic.find("CMDLINE_PORT=bad"), std::string::npos);
    EXPECT_NE(
        env_diagnostic.find("^-- 'bad' is not a valid port"),
        std::string::npos);

    auto env_with_default = try_parse_cmdline<
        R"(
        _Options
          --port=<port>  Listen port. [env: CMDLINE_PORT] [default: 443]
    )",
        EnvTypes>(1, argv);

    ASSERT_TRUE(env_with_default)
        << cmdline_error_message(env_with_default.error());
    EXPECT_EQ(env_with_default->opt<"--port">(), 443);
    ASSERT_EQ(env_with_default->warnings().size(), 1);

    SavedEnv channel_saved("CMDLINE_CHANNEL");
    setenv("CMDLINE_CHANNEL", "bad", 1);

    auto choice_env = try_parse_cmdline<R"(
        _Options
          --channel=[dev|prod]  Release channel. [env: CMDLINE_CHANNEL] [default: prod]
    )">(1, argv);

    ASSERT_TRUE(choice_env) << cmdline_error_message(choice_env.error());
    EXPECT_EQ(choice_env->opt<"--channel">(), "prod");
    ASSERT_EQ(choice_env->warnings().size(), 1);
    EXPECT_NE(
        cmdline_error_message(choice_env->warnings()[0])
            .find(
                "'bad' is not an allowed value for environment variable "
                "CMDLINE_CHANNEL"),
        std::string::npos);

    using DefaultTypes = TypeList<RuntimePortType>;

    auto fallback = try_parse_cmdline<
        R"(
        _Options
          --port=<runtime-port>  Listen port. [default: bad]
    )",
        DefaultTypes>(1, argv);

    ASSERT_FALSE(fallback);
    EXPECT_EQ(fallback.error().code, ErrorCode::kBadArgument);
    EXPECT_EQ(
        cmdline_error_message(fallback.error()),
        "'bad' is not a valid runtime-port for default for --port: "
        "invalid port");
    EXPECT_FALSE(fallback.error().location.has_location);
    std::string fallback_diagnostic = format_cmdline_error(
        fallback.error(), 1, argv, default_syntax_faces(), false);
    EXPECT_NE(fallback_diagnostic.find("[default: bad]"), std::string::npos);
    EXPECT_NE(
        fallback_diagnostic.find("^-- 'bad' is not a valid runtime-port"),
        std::string::npos);
}

TEST(CmdlineTest, ParseCmdlinePrintsInvalidEnvironmentWarnings) {
    SavedEnv saved("CMDLINE_PORT");
    setenv("CMDLINE_PORT", "bad", 1);

    const char* argv[] = {"server"};

    testing::internal::CaptureStderr();
    auto parsed = parse_cmdline<
        R"(
        _Options
          --port=<port>  Listen port. [env: CMDLINE_PORT]
    )",
        TypeList<PortType>>(1, argv);
    std::string warning = testing::internal::GetCapturedStderr();

    std::optional<int> port = parsed.opt<"--port">();
    EXPECT_FALSE(port);
    EXPECT_NE(
        warning.find("warning: ignoring invalid environment variable"),
        std::string::npos);
    EXPECT_NE(warning.find("CMDLINE_PORT=bad"), std::string::npos);
}

TEST(CmdlineTest, TryParseCmdlineIgnoresUnselectedRepeatValues) {
    const char* argv[] = {"server", "--port", "80", "--port", "bad"};

    using Types = TypeList<PortType>;

    auto parsed = try_parse_cmdline<
        R"(
        _Options
          --port=<port><  First port wins.
    )",
        Types>(5, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<int> port = parsed->opt<"--port">();
    ASSERT_TRUE(port);
    EXPECT_EQ(*port, 80);

    const char* last_argv[] = {"server", "--port", "bad", "--port", "443"};

    auto last = try_parse_cmdline<
        R"(
        _Options
          --port=<port>>  Last port wins.
    )",
        Types>(5, last_argv);

    ASSERT_TRUE(last) << cmdline_error_message(last.error());
    std::optional<int> last_port = last->opt<"--port">();
    ASSERT_TRUE(last_port);
    EXPECT_EQ(*last_port, 443);

    const char* choice_argv[] = {
        "server", "--channel", "bad", "--channel", "prod"};

    auto choice = try_parse_cmdline<R"(
        _Options
          --channel=[dev|prod]>  Last channel wins.
    )">(5, choice_argv);

    ASSERT_TRUE(choice) << cmdline_error_message(choice.error());
    std::optional<std::string> channel = choice->opt<"--channel">();
    ASSERT_TRUE(channel);
    EXPECT_EQ(*channel, "prod");
}

TEST(CmdlineTest, RejectsInvalidChoices) {
    const char* argv[] = {"sync", "--token", "secret", "--format",
                          "xml",  "src",     "dst",    "inc"};

    auto parsed = try_parse_cmdline<kSpec>(8, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kBadArgument);
}

TEST(CmdlineTest, RuntimeErrorContextPointsAtBadArgument) {
    const char* argv[] = {"sync", "--token", "secret", "--format",
                          "xml",  "src",     "dst",    "inc"};

    auto parsed = try_parse_cmdline<kSpec>(8, argv);

    ASSERT_FALSE(parsed);
    std::string error = format_cmdline_error(
        parsed.error(), 8, argv, default_syntax_faces(), false);
    EXPECT_TRUE(
        error.starts_with("\n │  error: command-line parsing failed\n"));
    EXPECT_NE(
        error.find("\n │  sync --token secret --format xml src dst inc"),
        std::string::npos);
    EXPECT_NE(
        error.find(
            "^-- 'xml' is not an allowed value for --format/-f "
            "(valid: json|yaml)"),
        std::string::npos);
}

TEST(CmdlineTest, RuntimeErrorContextQuotesArgumentsWithSpaces) {
    const char* argv[] = {"server", "bad value"};

    auto parsed = try_parse_cmdline<
        R"(
        _Arguments
          <port>
    )",
        TypeList<PortType>>(2, argv);

    ASSERT_FALSE(parsed);
    std::string error = format_cmdline_error(
        parsed.error(), 2, argv, default_syntax_faces(), false);
    EXPECT_NE(error.find("server 'bad value'"), std::string::npos);
    EXPECT_NE(
        error.find("      ^-- 'bad value' is not a valid port"),
        std::string::npos);
}

TEST(CmdlineTest, RuntimeErrorContextUsesUsageGutterForMissingArguments) {
    const char* argv[] = {"sync"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <input>  Input path.
    )">(1, argv);

    ASSERT_FALSE(parsed);
    std::string error = format_cmdline_error(
        parsed.error(), 1, argv, default_syntax_faces(), false);

    EXPECT_TRUE(
        error.starts_with("\n │  error: command-line parsing failed\n"));
    EXPECT_NE(error.find("\n │  sync\n"), std::string::npos);
    EXPECT_NE(
        error.find("^-- missing positional argument: input"),
        std::string::npos);
}

TEST(CmdlineTest, RejectsExtraPositionalArguments) {
    const char* argv[] = {"tool", "input", "extra"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <input>  Input path.
    )">(3, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kOverflow);
    EXPECT_EQ(
        cmdline_error_message(parsed.error()),
        "unexpected positional argument: extra");
    EXPECT_TRUE(parsed.error().location.has_location);
    EXPECT_EQ(parsed.error().location.argument_index, 2);
}

TEST(CmdlineTest, RuntimeErrorContextIsTrimmedAroundBadArgument) {
    std::string left(70, 'a');
    std::string right(70, 'b');
    const char* argv[] = {"tool", left.c_str(), right.c_str(), "--bad", "tail"};
    CmdlineError error{
        .code     = ErrorCode::kUnknown,
        .info     = subject_info(CmdlineErrorType::kUnknownOption, "--bad"),
        .location = ValueLocation::argv(3),
    };

    CmdlineContext context = make_cmdline_context(error, 5, argv);

    EXPECT_EQ(context.line.size(), kContextSize);
    EXPECT_NE(context.line.find("..."), std::string::npos);
    EXPECT_NE(context.line.find("--bad"), std::string::npos);
    EXPECT_EQ(context.line.find("tool "), std::string::npos);
}

TEST(CmdlineTest, RuntimeErrorContextKeepsEndOfLineCaretVisible) {
    std::string left(70, 'a');
    std::string right(70, 'b');
    const char* argv[] = {"tool", left.c_str(), right.c_str()};
    CmdlineError error{
        .code     = ErrorCode::kUnderflow,
        .info     = subject_info(CmdlineErrorType::kMissingArgument, "input"),
        .location = ValueLocation::argv(3),
    };

    CmdlineContext context = make_cmdline_context(error, 3, argv);

    EXPECT_EQ(context.line.size(), kContextSize);
    EXPECT_LT(context.column, context.line.size());
    EXPECT_NE(context.line.find("..."), std::string::npos);
}

TEST(CmdlineTest, ChoiceErrorsStopBeforeMissingPositionals) {
    const char* argv[] = {"publish", "project", "--channel", "bad"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <project>
          <asset>+

        _Options
          --channel=[dev|stage|prod]  Release channel.
    )">(4, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kBadArgument);
    EXPECT_TRUE(parsed.error().location.has_location);
    EXPECT_EQ(parsed.error().location.argument_index, 3);
    EXPECT_NE(
        cmdline_error_message(parsed.error())
            .find(
                "'bad' is not an allowed value for --channel "
                "(valid: dev|stage|prod)"),
        std::string::npos);
}

TEST(CmdlineTest, DashPrefixedArgumentsAreUnknownOptions) {
    const char* argv[] = {"tool", "project", "-asset"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <project>
          <asset>
    )">(3, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kUnknown);
    EXPECT_TRUE(parsed.error().location.has_location);
    EXPECT_EQ(parsed.error().location.argument_index, 2);
    EXPECT_NE(
        cmdline_error_message(parsed.error()).find("unknown option: -asset"),
        std::string::npos);
}

TEST(CmdlineTest, BareDashIsPositionalArgument) {
    const char* argv[] = {"copy", "-", "-"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <input:path>   Input path.
          <output:path>  Output path.
    )">(3, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->arg<"input">(), "-");
    EXPECT_EQ(parsed->arg<"output">(), "-");
}

TEST(CmdlineTest, OptionValuesMayStartWithDash) {
    const char* argv[] = {"tool", "--offset", "-1", "--name", "--dry-run"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --offset=  Offset.
          --name=    Name.
          --dry-run  Validate only.
    )">(5, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> offset = parsed->opt<"--offset">();
    ASSERT_TRUE(offset);
    EXPECT_EQ(*offset, "-1");
    std::optional<std::string> name = parsed->opt<"--name">();
    ASSERT_TRUE(name);
    EXPECT_EQ(*name, "--dry-run");
    EXPECT_FALSE(parsed->opt<"--dry-run">());
}

TEST(CmdlineTest, ShortOptionsAcceptEqualsInlineValues) {
    const char* argv[] = {"tool", "-o=value"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          -o=  Output.
    )">(2, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> output = parsed->opt<"-o">();
    ASSERT_TRUE(output);
    EXPECT_EQ(*output, "value");
    EXPECT_TRUE(parsed->set<"-o">());
}

TEST(CmdlineTest, ShortOptionsRejectAttachedValuesWithoutEquals) {
    const char* argv[] = {"tool", "-ovalue"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          -o=  Output.
    )">(2, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kUnknown);
    EXPECT_EQ(cmdline_error_message(parsed.error()), "unknown option: -ovalue");
}

TEST(CmdlineTest, RejectsMissingOptionValueAfterOptionToken) {
    const char* argv[] = {"tool", "--name"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --name=  Name.
    )">(2, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kIncomplete);
    EXPECT_EQ(cmdline_error_message(parsed.error()), "--name requires a value");
    EXPECT_TRUE(parsed.error().location.has_location);
    EXPECT_EQ(parsed.error().location.argument_index, 1);
}

TEST(CmdlineTest, ValueOptionsConsumeDoubleDashAsValue) {
    const char* argv[] = {"tool", "--marker", "--", "asset"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <asset>  Asset.

        _Options
          --marker=  Marker.
    )">(4, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> marker = parsed->opt<"--marker">();
    ASSERT_TRUE(marker);
    EXPECT_EQ(*marker, "--");
    EXPECT_EQ(parsed->arg<"asset">(), "asset");
}

TEST(CmdlineTest, DoubleDashDisablesSwitchMatching) {
    const char* argv[] = {"tool", "--", "-asset"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <asset>
    )">(3, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->arg<"asset">(), "-asset");
}

TEST(CmdlineTest, StarPositionalAcceptsZeroOrMoreValues) {
    const char* empty_argv[] = {"tool"};

    auto empty = try_parse_cmdline<R"(
        _Arguments
          <files>*  Files.
    )">(1, empty_argv);

    ASSERT_TRUE(empty) << cmdline_error_message(empty.error());
    EXPECT_TRUE(empty->arg<"files">().empty());

    const char* value_argv[] = {"tool", "one", "two"};

    auto values = try_parse_cmdline<R"(
        _Arguments
          <files>*  Files.
    )">(3, value_argv);

    ASSERT_TRUE(values) << cmdline_error_message(values.error());
    EXPECT_EQ(values->arg<"files">(), (std::vector<std::string>{"one", "two"}));

    std::string plain = strip_ansi(rendered_usage<R"(
        _Arguments
          <files>*  Files.
    )">("tool"));
    EXPECT_NE(plain.find("<files>*"), std::string::npos);
}

TEST(CmdlineTest, QuestionPositionalAcceptsZeroOrOneValue) {
    const char* empty_argv[] = {"tool"};

    auto empty = try_parse_cmdline<R"(
        _Arguments
          <config>?  Optional config.
    )">(1, empty_argv);

    ASSERT_TRUE(empty) << cmdline_error_message(empty.error());
    const std::optional<std::string>& missing = empty->arg<"config">();
    EXPECT_FALSE(missing);

    const char* value_argv[] = {"tool", "config.toml"};

    auto value = try_parse_cmdline<R"(
        _Arguments
          <config>?  Optional config.
    )">(2, value_argv);

    ASSERT_TRUE(value) << cmdline_error_message(value.error());
    const std::optional<std::string>& config = value->arg<"config">();
    ASSERT_TRUE(config);
    EXPECT_EQ(*config, "config.toml");

    const char* overflow_argv[] = {"tool", "one", "two"};

    auto overflow = try_parse_cmdline<R"(
        _Arguments
          <config>?  Optional config.
    )">(3, overflow_argv);

    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, ErrorCode::kOverflow);

    std::string plain = strip_ansi(rendered_usage<R"(
        _Arguments
          <config>?  Optional config.
    )">("tool"));
    EXPECT_NE(plain.find("<config>?"), std::string::npos);
}

TEST(CmdlineTest, ToggleableSwitchesUseLastInstance) {
    const char* argv[] = {"sync",         "--token", "secret", "--feature",
                          "--no-feature", "src",     "dst",    "inc"};

    auto parsed = try_parse_cmdline<kSpec>(8, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_FALSE(parsed->opt<"--feature">());
}

TEST(CmdlineTest, SetReportsWhetherOptionAppearedOnArgv) {
    const char* disabled_argv[] = {"tool", "--no-feature"};

    auto disabled = try_parse_cmdline<R"(
        _Options
          --feature>  Feature.
          --name=     Name. [default: default-name]
    )">(2, disabled_argv);

    ASSERT_TRUE(disabled) << cmdline_error_message(disabled.error());
    EXPECT_FALSE(disabled->opt<"--feature">());
    EXPECT_TRUE(disabled->set<"--feature">());
    EXPECT_EQ(disabled->opt<"--name">(), "default-name");
    EXPECT_FALSE(disabled->set<"--name">());

    const char* named_argv[] = {"tool", "--name", "custom"};

    auto named = try_parse_cmdline<R"(
        _Options
          --feature>  Feature.
          --name=     Name. [default: default-name]
    )">(3, named_argv);

    ASSERT_TRUE(named) << cmdline_error_message(named.error());
    EXPECT_EQ(named->opt<"--name">(), "custom");
    EXPECT_TRUE(named->set<"--name">());
    EXPECT_FALSE(named->set<"--feature">());
}

TEST(CmdlineTest, FirstAndLastRepeatPoliciesSelectExpectedValue) {
    const char* argv[] = {"sync",    "--cache", "one",     "--cache", "two",
                          "--first", "alpha",   "--first", "beta",    "--token",
                          "secret",  "src",     "dst",     "inc"};

    auto parsed = try_parse_cmdline<kSpec>(14, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> cache = parsed->opt<"--cache">();
    ASSERT_TRUE(cache);
    EXPECT_EQ(*cache, "two");
    std::optional<std::string> first = parsed->opt<"--first">();
    ASSERT_TRUE(first);
    EXPECT_EQ(*first, "alpha");
}

TEST(CmdlineTest, RequiresRequiredOptions) {
    const char* argv[] = {"sync", "src", "dst", "inc"};

    auto parsed = try_parse_cmdline<kSpec>(4, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kIncomplete);
}

TEST(CmdlineTest, RequiredValueOptionsReportMissingOptionValue) {
    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --token=  Access token. [required]
    )">(1, argv);

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::kIncomplete);
    EXPECT_EQ(
        cmdline_error_message(parsed.error()), "missing option value: --token");
}

TEST(CmdlineTest, RejectsRepeatedNonRepeatableOptions) {
    const char* switch_argv[] = {"tool", "--dry-run", "--dry-run"};

    auto repeated_switch = try_parse_cmdline<R"(
        _Options
          --dry-run  Validate only.
    )">(3, switch_argv);

    ASSERT_FALSE(repeated_switch);
    EXPECT_EQ(repeated_switch.error().code, ErrorCode::kAlreadySet);
    EXPECT_EQ(
        cmdline_error_message(repeated_switch.error()),
        "--dry-run was specified more than once");
    EXPECT_TRUE(repeated_switch.error().location.has_location);
    EXPECT_EQ(repeated_switch.error().location.argument_index, 2);

    const char* value_argv[] = {"tool", "--name", "one", "--name", "two"};

    auto repeated_value = try_parse_cmdline<R"(
        _Options
          --name=  Name.
    )">(5, value_argv);

    ASSERT_FALSE(repeated_value);
    EXPECT_EQ(repeated_value.error().code, ErrorCode::kAlreadySet);
    EXPECT_EQ(
        cmdline_error_message(repeated_value.error()),
        "--name was specified more than once");
    EXPECT_TRUE(repeated_value.error().location.has_location);
    EXPECT_EQ(repeated_value.error().location.argument_index, 4);
}

TEST(CmdlineTest, TryParseCmdlineTreatsHelpAsEscapeHatch) {
    const char* long_argv[] = {
        "sync", "--bad", "--format", "xml", "--help",
    };

    auto long_help = try_parse_cmdline<kSpec>(5, long_argv);

    ASSERT_TRUE(long_help) << cmdline_error_message(long_help.error());
    EXPECT_TRUE(long_help->opt<"--help">());
    EXPECT_TRUE(long_help->opt<"-h">());

    const char* short_argv[] = {
        "sync", "--bad", "--format", "xml", "-h",
    };

    auto short_help = try_parse_cmdline<kSpec>(5, short_argv);

    ASSERT_TRUE(short_help) << cmdline_error_message(short_help.error());
    EXPECT_TRUE(short_help->opt<"--help">());
    EXPECT_TRUE(short_help->opt<"-h">());

    const char* grouped_argv[] = {
        "sync", "--bad", "--format", "xml", "-vh",
    };

    auto grouped_help = try_parse_cmdline<kSpec>(5, grouped_argv);

    ASSERT_TRUE(grouped_help) << cmdline_error_message(grouped_help.error());
    EXPECT_TRUE(grouped_help->opt<"--help">());
    EXPECT_TRUE(grouped_help->set<"--help">());
}

TEST(CmdlineTest, CustomHelpLineKeepsHelpEscapeHatch) {
    const char* long_argv[] = {"tool", "--help"};

    auto long_help = try_parse_cmdline<R"(
        _Options
          --help  Show custom help.
    )">(2, long_argv);

    ASSERT_TRUE(long_help) << cmdline_error_message(long_help.error());
    EXPECT_TRUE(long_help->opt<"--help">());
    EXPECT_TRUE(long_help->opt<"-h">());

    const char* short_argv[] = {"tool", "-h"};

    auto short_help = try_parse_cmdline<R"(
        _Options
          --help  Show custom help.
    )">(2, short_argv);

    ASSERT_TRUE(short_help) << cmdline_error_message(short_help.error());
    EXPECT_TRUE(short_help->opt<"--help">());
    EXPECT_TRUE(short_help->opt<"-h">());

    auto short_only_help = try_parse_cmdline<R"(
        _Options
          -h  Show custom help.
    )">(2, short_argv);

    ASSERT_TRUE(short_only_help)
        << cmdline_error_message(short_only_help.error());
    EXPECT_TRUE(short_only_help->opt<"--help">());
    EXPECT_TRUE(short_only_help->opt<"-h">());
}

TEST(CmdlineTest, DoubleDashStopsHelpEscapeHatch) {
    const char* argv[] = {"tool", "--", "--help"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <input>  Input.
    )">(3, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_FALSE(parsed->opt<"--help">());
    EXPECT_FALSE(parsed->opt<"-h">());
    EXPECT_EQ(parsed->arg<"input">(), "--help");
}

TEST(CmdlineTest, HelpSwitchRejectsInlineValues) {
    const char* long_argv[] = {"sync", "--help=true"};

    auto long_help = try_parse_cmdline<kSpec>(2, long_argv);

    ASSERT_FALSE(long_help);
    EXPECT_EQ(long_help.error().code, ErrorCode::kBadArgument);
    EXPECT_EQ(
        cmdline_error_message(long_help.error()),
        "--help does not take a value");

    const char* short_argv[] = {"sync", "-h=true"};

    auto short_help = try_parse_cmdline<kSpec>(2, short_argv);

    ASSERT_FALSE(short_help);
    EXPECT_EQ(short_help.error().code, ErrorCode::kBadArgument);
    EXPECT_EQ(
        cmdline_error_message(short_help.error()), "-h does not take a value");

    const char* grouped_argv[] = {"sync", "-vh=true"};

    auto grouped_help = try_parse_cmdline<kSpec>(2, grouped_argv);

    ASSERT_FALSE(grouped_help);
    EXPECT_EQ(grouped_help.error().code, ErrorCode::kUnknown);
    EXPECT_EQ(
        cmdline_error_message(grouped_help.error()),
        "unknown option: -vh=true");
}

TEST(CmdlineTest, ParseCmdlinePrintsHelpAndExits) {
    const char* long_argv[] = {"sync", "--help"};

    EXPECT_EXIT(
        { (void)parse_cmdline<kSpec>(2, long_argv); },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");

    const char* short_argv[] = {"sync", "-h"};

    EXPECT_EXIT(
        { (void)parse_cmdline<kSpec>(2, short_argv); },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");

    const char* bad_argv[] = {"sync", "--bad", "--format", "xml", "--help"};

    EXPECT_EXIT(
        { (void)parse_cmdline<kSpec>(5, bad_argv); },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(CmdlineTest, ParseCmdlinePrintsErrorsAndExits) {
    const char* argv[] = {"sync", "src"};

    EXPECT_EXIT(
        {
            (void)parse_cmdline<R"(
                _Arguments
                  <source>  Source path.
                  <dest>    Destination path.
            )">(2, argv);
        },
        ::testing::ExitedWithCode(static_cast<int>(ErrorCode::kUnderflow)),
        "missing positional argument: dest");
}

TEST(CmdlineTest, HidesHiddenOptionsFromHelp) {
    std::string help = strip_ansi(rendered_usage<kSpec>("sync"));
    EXPECT_EQ(help.find("--quiet"), std::string::npos);
    EXPECT_NE(help.find("--dry-run"), std::string::npos);
}

TEST(CmdlineTest, CommentExampleFacadeCompilesAndQueriesValues) {
    const char* argv[] = {"sync", "--dry-run", "input", "output", "one",
                          "two",  "--tag",     "alpha", "--tag",  "beta"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <input>   Source directory to scan
          <output>  Remote target path
          <paths>+  File or directory paths to transfer

        _Options
          --tag/-t=<name>+  Attach one or more tags
          --dry-run        Validate without writing remote changes
    )">(10, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->arg<"input">(), "input");
    EXPECT_EQ(parsed->arg<"output">(), "output");
    EXPECT_EQ(parsed->arg<"paths">(), (std::vector<std::string>{"one", "two"}));
    EXPECT_EQ(
        parsed->opt<"--tag">(), (std::vector<std::string>{"alpha", "beta"}));
    EXPECT_TRUE(parsed->opt<"--dry-run">());
}

TEST(CmdlineTest, UnknownTypeNamesFallbackToStrings) {
    const char* argv[] = {
        "copy", "--output", "out/file.txt", "--dry-run", "in/file.txt"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <input:path>  Input path.

        _Options
          --output=<path>  Output path.
          --dry-run        Validate only.
    )">(5, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->arg<"input">(), "in/file.txt");
    std::optional<std::string> output = parsed->opt<"--output">();
    ASSERT_TRUE(output);
    EXPECT_EQ(*output, "out/file.txt");
    EXPECT_TRUE(parsed->opt<"--dry-run">());
}

TEST(CmdlineTest, CmdLineParsesScalarVectorAndCountValues) {
    const char* argv[] = {"sync",   "--tag", "one", "--tag", "two",  "--token",
                          "secret", "-vv",   "src", "dst",   "inc1", "inc2"};

    auto parsed = try_parse_cmdline<R"(
        _Arguments
          <source:path>       Source path.
          <destination:path>  Destination path.
          <include:path>+     Extra include paths.

        _Options
          --tag/-t=<name>+    Repeatable tags.
          --token=            Access token.
          --verbose/-v+       Increase verbosity.
    )">(12, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->arg<"source">(), "src");
    EXPECT_EQ(parsed->arg<"destination">(), "dst");
    EXPECT_EQ(
        parsed->arg<"include">(), (std::vector<std::string>{"inc1", "inc2"}));
    EXPECT_EQ(parsed->opt<"--tag">(), (std::vector<std::string>{"one", "two"}));
    std::optional<std::string> token = parsed->opt<"--token">();
    ASSERT_TRUE(token);
    EXPECT_EQ(*token, "secret");
    EXPECT_EQ(parsed->opt<"-v">(), 2);
}

TEST(CmdlineTest, OptionalTypedValuesReturnOptional) {
    constexpr char kOptionalSpec[] = R"(
        _Options
          --port=<port>  Optional port.
    )";

    const char* empty_argv[] = {"tool"};
    auto empty =
        try_parse_cmdline<kOptionalSpec, TypeList<PortType>>(1, empty_argv);

    ASSERT_TRUE(empty) << cmdline_error_message(empty.error());
    std::optional<int> missing = empty->opt<"--port">();
    EXPECT_FALSE(missing);

    const char* value_argv[] = {"tool", "--port", "443"};
    auto value =
        try_parse_cmdline<kOptionalSpec, TypeList<PortType>>(3, value_argv);

    ASSERT_TRUE(value) << cmdline_error_message(value.error());
    std::optional<int> port = value->opt<"--port">();
    ASSERT_TRUE(port);
    EXPECT_EQ(*port, 443);
}

TEST(CmdlineTest, EnvOnlyTypedValuesReturnOptional) {
    constexpr char kOptionalSpec[] = R"(
        _Options
          --port=<port>  Optional port. [env: CMDLINE_OPTIONAL_PORT]
    )";

    SavedEnv saved("CMDLINE_OPTIONAL_PORT");
    unsetenv("CMDLINE_OPTIONAL_PORT");

    const char* empty_argv[] = {"tool"};
    auto empty =
        try_parse_cmdline<kOptionalSpec, TypeList<PortType>>(1, empty_argv);

    ASSERT_TRUE(empty) << cmdline_error_message(empty.error());
    std::optional<int> missing = empty->opt<"--port">();
    EXPECT_FALSE(missing);

    setenv("CMDLINE_OPTIONAL_PORT", "443", 1);
    auto value =
        try_parse_cmdline<kOptionalSpec, TypeList<PortType>>(1, empty_argv);

    ASSERT_TRUE(value) << cmdline_error_message(value.error());
    std::optional<int> port = value->opt<"--port">();
    ASSERT_TRUE(port);
    EXPECT_EQ(*port, 443);
}

TEST(CmdlineTest, DefaultedTypedValuesReturnValue) {
    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<
        R"(
        _Options
          --port=<port>  Defaulted port. [default: 443]
    )",
        TypeList<PortType>>(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    int port = parsed->opt<"--port">();
    EXPECT_EQ(port, 443);
}

TEST(CmdlineTest, QuotedStringDefaultsCanContainWhitespace) {
    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --name=  Name. [default: "two words"]
    )">(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->opt<"--name">(), "two words");
}

TEST(CmdlineTest, QuotedStringDefaultsCanEscapeQuotes) {
    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --name=  Name. [default: "say \"hello\""]
    )">(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->opt<"--name">(), R"(say "hello")");
}

TEST(CmdlineTest, QuotedStringDefaultsCanEscapeBackslashes) {
    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<R"(
        _Options
          --path=  Path. [default: "C:\\Temp"]
    )">(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->opt<"--path">(), R"(C:\Temp)");
}

TEST(CmdlineTest, QuotedDefaultsUnescapeBeforeCompileTimeValidation) {
    const char* argv[] = {"tool"};

    auto parsed = try_parse_cmdline<
        R"(
        _Options
          --name=<quote-default>  Name. [default: "say \"hello\""]
    )",
        TypeList<QuoteDefaultType>>(1, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_TRUE(parsed->opt<"--name">());
}

TEST(CmdlineTest, LaterTypeListsOverrideEarlierTypes) {
    const char* argv[] = {"tool", "--name", "option", "arg"};

    using EarlierTypes = TypeList<FirstWidgetType>;
    using LaterTypes   = TypeList<LastWidgetType>;

    std::string help = strip_ansi(
        rendered_usage<kOverrideSpec, EarlierTypes, LaterTypes>("tool"));
    EXPECT_NE(help.find("<input>"), std::string::npos);
    EXPECT_NE(help.find("<widget>"), std::string::npos);
    EXPECT_EQ(help.find("<last-widget:widget>"), std::string::npos);

    auto parsed =
        try_parse_cmdline<kOverrideSpec, EarlierTypes, LaterTypes>(4, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    EXPECT_EQ(parsed->arg<"input">(), "arg");
    std::optional<std::string> name = parsed->opt<"--name">();
    ASSERT_TRUE(name);
    EXPECT_EQ(*name, "last-widget:option");
}

TEST(CmdlineTest, CustomTypesOverrideBuiltInTypes) {
    const char* argv[] = {"tool", "--name", "input.txt"};

    auto parsed = try_parse_cmdline<
        R"(
        _Options
          --name=  Name.
    )",
        TypeList<OverrideStringType>>(3, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> name = parsed->opt<"--name">();
    ASSERT_TRUE(name);
    EXPECT_EQ(*name, "custom-string:input.txt");
}

TEST(CmdlineTest, LaterTypesInSameListOverrideEarlierTypes) {
    const char* argv[] = {"tool", "--name", "option", "arg"};

    using Types = TypeList<FirstWidgetType, LastWidgetType>;

    std::string help = strip_ansi(rendered_usage<kOverrideSpec, Types>("tool"));
    EXPECT_NE(help.find("<widget>"), std::string::npos);
    EXPECT_EQ(help.find("<last-widget:widget>"), std::string::npos);

    auto parsed = try_parse_cmdline<kOverrideSpec, Types>(4, argv);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> name = parsed->opt<"--name">();
    ASSERT_TRUE(name);
    EXPECT_EQ(*name, "last-widget:option");
}

TEST(CmdlineTest, RuntimeFacesStyleWithoutChangingParsedType) {
    const char* argv[] = {"tool", "--name", "option", "arg"};

    using Types                    = TypeList<LastWidgetType>;
    std::array<UsageFace, 1> faces = {
        UsageFace{"widget", fmt::fg(fmt::rgb(1, 2, 3))}
    };

    auto spec = runtime_cmdline<kOverrideSpec, Types>(faces);
    std::string help =
        strip_ansi(rendered_usage<kOverrideSpec, Types>("tool", faces));
    EXPECT_NE(help.find("<widget>"), std::string::npos);
    EXPECT_EQ(
        styled(find_face(spec.faces(), "widget"), "x", true),
        styled(fmt::fg(fmt::rgb(1, 2, 3)), "x", true));

    auto parsed = try_parse_cmdline<kOverrideSpec, Types>(4, argv, faces);

    ASSERT_TRUE(parsed) << cmdline_error_message(parsed.error());
    std::optional<std::string> name = parsed->opt<"--name">();
    ASSERT_TRUE(name);
    EXPECT_EQ(*name, "last-widget:option");
}

TEST(CmdlineTest, RuntimeFacesOverrideBuiltInFaces) {
    std::array<UsageFace, 1> faces = {
        UsageFace{"input", fmt::fg(fmt::rgb(4, 5, 6))}
    };

    auto spec = runtime_cmdline<kOverrideSpec>(faces);

    EXPECT_EQ(
        styled(find_face(spec.faces(), "input"), "x", true),
        styled(fmt::fg(fmt::rgb(4, 5, 6)), "x", true));
}

TEST(CmdlineTest, RuntimeFacesOverrideSyntaxFaces) {
    std::array<UsageFace, 1> faces = {
        UsageFace{"cmdline-usage", fmt::fg(fmt::rgb(7, 8, 9))}
    };

    auto spec = runtime_cmdline<kOverrideSpec>(faces);

    EXPECT_EQ(
        styled(find_face(spec.faces(), "cmdline-usage"), "x", true),
        styled(fmt::fg(fmt::rgb(7, 8, 9)), "x", true));
}

}  // namespace

}  // namespace cmd
