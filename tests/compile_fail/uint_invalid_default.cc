#include "cmdline.h"

int main() {
    const char* argv[] = {"tool"};

    (void)cmd::try_parse_cmdline<R"(
        _Options
          --limit=<uint>  Limit to use. [default: -1]
    )">(1, argv);
}
