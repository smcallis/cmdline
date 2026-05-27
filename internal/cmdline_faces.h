#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fmt/color.h>
#include <fmt/format.h>

#include "cmdline_rules.h"

// Defines functions for defining and using faces. Faces are purely stylistic
// for printing different semantic elements. Face names prefixed with cmdline-
// are reserved for the implementations usage. Types (including user-provided
// types) have a face with the same name as the type.

namespace cmd {

// Stores one runtime usage-text face. Faces only affect diagnostics and help
// output. They never change the C++ type selected by cmdline_typed.h.
struct UsageFace {
    std::string_view name;
    fmt::text_style style{};
};

// Maps face names to terminal styles.
using FaceMap = std::unordered_map<std::string, fmt::text_style>;

// Returns true when a style has no foreground, background, or emphasis.
inline bool empty_style(fmt::text_style style) {
    return !style.has_foreground() && !style.has_background() &&
           !style.has_emphasis();
}

// Applies a style only when color output is enabled.
inline std::string styled(
    fmt::text_style style, std::string_view text, bool color) {
    if (!color || empty_style(style)) {
        return std::string(text);
    }
    return fmt::format(style, "{}", text);
}

// Returns the style for `name`, or an empty style when no face is defined.
inline fmt::text_style find_face(const FaceMap& faces, std::string_view name) {
    auto found = faces.find(std::string(name));
    return found == faces.end() ? fmt::text_style{} : found->second;
}

// Adds face definitions, replacing any existing face with the same name.
inline void add_faces(FaceMap& faces, std::span<const UsageFace> extra) {
    for (UsageFace face : extra) {
        faces.insert_or_assign(std::string(face.name), face.style);
    }
}

// Returns the built-in syntax faces used before type faces and user overrides.
inline FaceMap default_syntax_faces() {
    return {
        // Styles the word "Usage" at the top of generated usage text.
        {"cmdline-usage",
         fmt::emphasis::bold | fmt::fg(fmt::rgb(0xd0, 0xd0, 0xd0))},

        // Styles the colon after "Usage".
        {"cmdline-usage-separator", {}},

        // Styles the built-in --help/-h switch in usage and option lists.
        {"cmdline-help", {}},

        // Styles display headings parsed from lines starting with '_'.
        {"cmdline-heading", fmt::emphasis::underline},

        // Styles the left gutter rail that frames generated usage text.
        {"cmdline-rail", fmt::fg(fmt::color::dim_gray)},

        // Styles the rail next to option descriptions.
        {"cmdline-description-rail", fmt::fg(fmt::color::dim_gray)},

        // Styles angle brackets around positional argument names.
        {"cmdline-arg-bracket", {}},

        // Styles positional argument cardinality markers.
        {"cmdline-arg-repeat", fmt::fg(fmt::color::indian_red)},

        // Styles option and switch names.
        {"cmdline-opt-name", {}},

        // Styles separators between long and short option names.
        {"cmdline-opt-separator", {}},

        // Styles the [no-] marker for toggleable switches.
        {"cmdline-opt-toggle", {}},

        // Styles the '=' between option names and value placeholders.
        {"cmdline-opt-value-separator", {}},

        // Styles angle brackets around typed option value placeholders.
        {"cmdline-opt-value-bracket", {}},

        // Styles square brackets around option choice lists.
        {"cmdline-opt-choice-bracket", {}},

        // Styles '|' separators inside option choice lists.
        {"cmdline-opt-choice-separator", {}},

        // Styles repeat policy markers on options.
        {"cmdline-opt-repeat", fmt::fg(fmt::color::silver)},

        // Styles square brackets around rendered option attributes.
        {"cmdline-attr-bracket", {}},

        // Styles the "default: " label in rendered option attributes.
        {"cmdline-attr-default-label", {}},

        // Styles default values in rendered option attributes.
        {"cmdline-attr-default-value",
         fmt::fg(fmt::color::golden_rod) | fmt::emphasis::bold},

        // Styles the source context line in runtime error diagnostics.
        {"cmdline-error-context", {}},

        // Styles the "error:" label in runtime error diagnostics.
        {"cmdline-error-label",
         fmt::emphasis::bold | fmt::fg(fmt::color::indian_red)},

        // Styles the message body in runtime error diagnostics.
        {"cmdline-error-message", {}},

        // Styles the caret marker in runtime error diagnostics.
        {"cmdline-error-caret",
         fmt::emphasis::bold | fmt::fg(fmt::color::indian_red)},

        // Styles the source context line in runtime warning diagnostics.
        {"cmdline-warning-context", {}},

        // Styles the "warning:" label in runtime warning diagnostics.
        {"cmdline-warning-label",
         fmt::emphasis::bold | fmt::fg(fmt::color::gold)},

        // Styles the message body in runtime warning diagnostics.
        {"cmdline-warning-message", {}},

        // Styles the caret marker in runtime warning diagnostics.
        {"cmdline-warning-caret",
         fmt::emphasis::bold | fmt::fg(fmt::color::gold)},
    };
}

// Returns the built-in type and semantic faces used before user overrides.
// Type faces share names with value types, while semantic faces such as "input"
// and "output" can be used as display-only labels in a spec.
inline std::array<UsageFace, 7> default_type_faces() {
    return {
        UsageFace{kDefaultTypeName, {}                             },
        UsageFace{"int",            {}                             },
        UsageFace{"uint",           {}                             },
        UsageFace{"float",          {}                             },
        UsageFace{"double",         {}                             },
        UsageFace{"input",          fmt::fg(fmt::color::steel_blue)},
        UsageFace{"output",         fmt::fg(fmt::color::salmon)    },
    };
}

// Returns syntax and type faces with caller-provided overrides applied last.
inline FaceMap default_faces(std::span<const UsageFace> override = {}) {
    FaceMap faces   = default_syntax_faces();
    auto type_faces = default_type_faces();
    add_faces(faces, type_faces);
    add_faces(faces, override);
    return faces;
}

}  // namespace cmd
