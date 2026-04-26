#include <string>
#include <vector>

#include <fmt/format.h>

#include "cmdline.h"

#include <string>
#include <vector>

#include "cmdline.h"

int main(int argc, const char* argv[]) {
    auto cmdline = cmd::parse_cmdline<R"(
        Sync files to a destination.

        _Arguments
          <source:input>  Source directory
          <dest:output>    Destination directory
          <paths:input>*  Extra files or directories to include

        _Options
          --config/-c=<input>   Config file [env: SYNC_CONFIG] [default: sync.toml]
          --format=[json|text]  Output format [default: text]
          --tag/-t=<tag>+       Attach one or more tags
          --dry-run             Print planned work without writing files
          --verbose/-v+         Increase logging verbosity
    )">(argc, argv);

    const std::string& source = cmdline.arg<"source">();
    const std::string& dest = cmdline.arg<"dest">();
    const std::vector<std::string>& paths = cmdline.arg<"paths">();

    const std::string& config = cmdline.opt<"--config">();
    const std::string& format = cmdline.opt<"--format">();
    const std::vector<std::string>& tags = cmdline.opt<"--tag">();

    bool dry_run = cmdline.opt<"--dry-run">();
    int verbosity = cmdline.opt<"-v">();

    // return sync_files(
    //     source, dest, paths, config, format, tags, dry_run, verbosity);
}

// constexpr char kSpec[] = R"(
//     publish assets into a release directory.
//     This example intentionally uses most of the spec grammar.

//     _Arguments

//       <project:input>    Project root to scan.
//       <release:output>   Release directory to create or update.
//       <assets:input>+    Asset files or directories to publish.

//     _Options
//      _Build

//       --config/-c=<input>       Config file to read. [env: PUBLISH_CONFIG]
//       --old-config=<input>      Legacy config file path.
//       --manifest/-m=<output>    Manifest path to write. [default: manifest.json]
//       --channel/-r=[dev|stage|prod]  Release channel. [default: stage]
//       --define/-D=<define>+     Build define in key=value form.

//      _Selection

//       --tag/-t=<tag>+           Attach one or more release tags.
//       --profile=<profile><      Use the first selected profile.
//       --cache=<output>>         Use the last selected cache directory.

//      _Behavior

//       --dry-run                 Print planned work without writing files.
//       --watch>                  Enable or disable watch mode with --no-watch.
//       --verbose/-v+             Increase logging verbosity.

//      _Advanced

//       --token=                  Secret token. [env: PUBLISH_TOKEN] [hidden]
//       --legacy-mode             Compatibility mode.
//       --docs                    Open documentation.
// )";

// // Joins strings for compact example output.
// std::string join_strings(const std::vector<std::string>& values) {
//     std::string joined;
//     for (const auto& value : values) {
//         if (!joined.empty()) {
//             joined += ", ";
//         }
//         joined += value;
//     }
//     return joined;
// }

// int main(int argc, const char* argv[]) {
//     auto parsed = cmd::parse_cmdline<kSpec>(argc, argv);

//     fmt::print("project: {}\n", parsed.arg<"project">());
//     fmt::print("release: {}\n", parsed.arg<"release">());
//     fmt::print("assets: {}\n", join_strings(parsed.arg<"assets">()));
//     fmt::print("manifest: {}\n", parsed.opt<"--manifest">());
//     fmt::print("channel: {}\n", parsed.opt<"--channel">());
//     fmt::print("defines: {}\n", join_strings(parsed.opt<"--define">()));
//     fmt::print("tags: {}\n", join_strings(parsed.opt<"--tag">()));
//     if (const auto& profile = parsed.opt<"--profile">()) {
//         fmt::print("profile: {}\n", *profile);
//     }
//     if (const auto& cache = parsed.opt<"--cache">()) {
//         fmt::print("cache: {}\n", *cache);
//     }
//     fmt::print("dry-run: {}\n", parsed.opt<"--dry-run">());
//     fmt::print("watch: {}\n", parsed.opt<"--watch">());
//     fmt::print("verbosity: {}\n", parsed.opt<"--verbose">());
// }
