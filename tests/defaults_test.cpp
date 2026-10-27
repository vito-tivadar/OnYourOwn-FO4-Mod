// The shipped settings.ini must match the built-in defaults.
// Usage: defaults_test [path/to/settings.ini] (default: path relative to the repository root)
#include "IniSettings.h"

#include <cstdio>

int main(int argc, char** argv)
{
    using namespace OnYourOwn;
    const char*  path = argc > 1 ? argv[1] : "Data/MCM/Config/OnYourOwn/settings.ini";
    const Config shipped = LoadConfig(path, "");
    const bool   same = shipped == Config{};
    std::printf(same ? "shipped defaults match built-in defaults\n" : "MISMATCH between %s and built-in defaults\n", path);
    return same ? 0 : 1;
}
