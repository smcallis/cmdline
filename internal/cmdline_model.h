#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmdline_rules.h"

// Defines the runtime command-spec model shared by argv parsing, typed
// materialization, and usage generation.
//
// This file stores owned strings and vectors for positional arguments, options,
// and preserved spec lines.
//
// Compile-time syntax parsing and source-located validation live in
// cmdline_parse.h and cmdline_check.h, respectively.

namespace cmd {

// Describes one positional argument declared by a command-line spec.
struct PosArg {
    std::string name;
    std::string type_name{kDefaultTypeName};
    std::string value_label;
    std::string face_name{kDefaultTypeName};
    std::string description;
    bool accepts_many = false;
    bool required     = true;

    // Returns the display label used in generated usage text.
    std::string_view usage_label() const {
        if (!value_label.empty()) {
            return value_label;
        }
        return name;
    }

    // Returns the display width of the positional usage token.
    size_t usage_width() const {
        return usage_label().size() + 2 + accepts_many;
    }
};

// Describes one option or switch declared by a command-line spec.
struct Option {
    std::string long_name;
    std::string short_name;
    std::string description;
    std::string type_name{kDefaultTypeName};
    std::string value_label_name;
    std::string value_face_name{kDefaultTypeName};
    std::string default_value;
    std::string env_name;
    std::vector<std::string> choices;
    RepeatPolicy repeat    = RepeatPolicy::kError;
    bool takes_value       = false;
    bool count_occurrences = false;
    bool required          = false;
    bool hidden            = false;

    // Returns the canonical long option spelling.
    std::string long_form() const {
        return long_name.empty() ? "" : "--" + long_name;
    }

    // Returns the canonical short option spelling.
    std::string short_form() const {
        return short_name.empty() ? "" : "-" + short_name;
    }

    // Returns the combined user-facing option spelling.
    std::string full_name() const {
        if (!long_name.empty() && !short_name.empty()) {
            std::string name = long_form();
            name += "/";
            name += short_form();
            return name;
        }

        if (!long_name.empty()) {
            return long_form();
        }

        return short_form();
    }

    // Returns the display width of the option value placeholder.
    size_t value_label_width() const {
        if (!takes_value) {
            return 0;
        }

        if (!choices.empty()) {
            size_t width = 2;
            for (size_t index = 0; index < choices.size(); ++index) {
                width += choices[index].size();
                if (index != 0) {
                    ++width;
                }
            }
            return width;
        }

        if (value_label_name.empty()) {
            return 0;
        }
        return value_label_name.size() + 2;
    }

    // Returns the display width of the option usage token.
    size_t usage_width() const {
        size_t width =
            long_name.empty() ? short_form().size() : long_usage_width();
        if (!long_name.empty() && !short_name.empty()) {
            width += 1 + short_form().size();
        }

        if (takes_value) {
            width += 1 + value_label_width();
        }
        return width;
    }

    // Returns whether this switch accepts the implicit '--no-' spelling.
    bool has_negative_form() const {
        return !long_name.empty() && !takes_value &&
               (repeat == RepeatPolicy::kFirst ||
                repeat == RepeatPolicy::kLast);
    }

    // Returns the display width of the long option usage form.
    size_t long_usage_width() const {
        return has_negative_form() ? long_name.size() + 7 : long_form().size();
    }
};

// Preserves one parsed spec line for generated usage text.
struct SpecLine {
    LineType type = LineType::kBlank;
    PosArg pos_arg;
    Option option;
    std::string text;
    size_t indent = 0;

    // Returns a blank preserved spec line.
    static SpecLine make_blank() { return {}; }

    // Returns a preserved heading line.
    static SpecLine make_heading(std::string text, size_t line_indent) {
        SpecLine line;
        line.type   = LineType::kHeading;
        line.text   = std::move(text);
        line.indent = line_indent;
        return line;
    }

    // Returns a preserved raw-text help line.
    static SpecLine make_raw_text(std::string text, size_t line_indent) {
        SpecLine line;
        line.type   = LineType::kRawText;
        line.text   = std::move(text);
        line.indent = line_indent;
        return line;
    }

    // Returns a preserved positional-argument line.
    static SpecLine make_pos_arg(PosArg arg, size_t line_indent) {
        SpecLine line;
        line.type    = LineType::kArg;
        line.pos_arg = std::move(arg);
        line.indent  = line_indent;
        return line;
    }

    // Returns a preserved option line.
    static SpecLine make_option(Option option, size_t line_indent = 0) {
        SpecLine line;
        line.type   = LineType::kOption;
        line.option = std::move(option);
        line.indent = line_indent;
        return line;
    }
};

// Returns the built-in help switch metadata for generated usage.
inline Option help_option() {
    Option option;
    option.long_name   = std::string(kHelpLongName);
    option.short_name  = std::string(kHelpShortName);
    option.description = std::string(kHelpDescription);
    return option;
}

// Returns whether an option is the built-in help switch customization point.
inline bool is_help_option(const Option& option) {
    return uses_only_help_names(option.long_name, option.short_name);
}

// Appends the built-in help switch when no user help line was declared.
inline std::vector<SpecLine> lines_with_help(
    const std::vector<SpecLine>& lines) {
    for (const SpecLine& line : lines) {
        if (line.type == LineType::kOption && is_help_option(line.option)) {
            return lines;
        }
    }

    std::vector<SpecLine> result = lines;
    result.push_back(SpecLine::make_option(help_option()));
    return result;
}

}  // namespace cmd
