#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

// Provides shared test-only helpers for environment management and usage-text
// assertions.

namespace cmd_test {

struct SavedEnv {
    std::string name;
    bool had_value = false;
    std::string saved_value;

    explicit SavedEnv(const char* env_name) : name(env_name) {
        const char* current = std::getenv(name.c_str());
        had_value           = current != nullptr;
        if (had_value) {
            saved_value = current;
        }
    }

    SavedEnv(const SavedEnv&)            = delete;
    SavedEnv& operator=(const SavedEnv&) = delete;
    SavedEnv(SavedEnv&&)                 = delete;
    SavedEnv& operator=(SavedEnv&&)      = delete;

    ~SavedEnv() {
        if (!had_value) {
            unsetenv(name.c_str());
        } else {
            setenv(name.c_str(), saved_value.c_str(), 1);
        }
    }
};

struct SavedColorEnv {
    SavedEnv no_color{"NO_COLOR"};
    SavedEnv force_color{"FORCE_COLOR"};

    void clear() const {
        unsetenv("NO_COLOR");
        unsetenv("FORCE_COLOR");
    }

    void force_color_output() const { setenv("FORCE_COLOR", "1", 1); }
};

inline std::string strip_ansi(std::string_view text) {
    std::string output;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && (text[i] < '@' || text[i] > '~')) {
                ++i;
            }
            if (i < text.size()) {
                ++i;
            }
            continue;
        }

        output.push_back(text[i]);
        ++i;
    }
    return output;
}

}  // namespace cmd_test
