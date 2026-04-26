#include "cmdline.h"

int main() {
    const char* argv[] = {"tool"};

    (void)cmd::try_parse_cmdline<R"(
        _Options
          --count=<int>  Count to use. [default: nope]
    )">(1, argv);
}
