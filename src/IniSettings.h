#pragma once

#include "CompassPolicy.h"
#include "MarkerPolicy.h"

#include <cstdint>
#include <filesystem>
#include <iosfwd>

namespace OnYourOwn
{
    // Relative to the game folder (the process working directory), as MCM uses them.
    inline constexpr const char* kDefaultIniPath = "Data/MCM/Config/OnYourOwn/settings.ini";
    inline constexpr const char* kUserIniPath = "Data/MCM/Settings/OnYourOwn.ini";

    // Diagnostic logging choice. Values are the MCM dropdown indices; do not reorder.
    // Startup, settings, warnings and errors are always logged.
    enum class LogLevel : std::int32_t
    {
        None = 0,
        Everything = 1,
        Locations = 2,
        Quests = 3,
        MarkerHiding = 4,
        MapView = 5,
        Compass = 6
    };

    enum class LogCategory
    {
        Locations,     // location and settlement decisions
        Quests,        // quest types and groups
        MarkerHiding,  // SWF marker classes and removed quest/player entries
        MapView,       // world map zoom/center while the player is hidden
        Compass        // HUD compass and floating markers
    };

    [[nodiscard]] bool ShouldLog(LogLevel level, LogCategory category) noexcept;

    struct Config
    {
        bool            enabled = true;  // false = vanilla behaviour
        LogLevel        logLevel = LogLevel::None;
        Settings        markers{};
        CompassSettings compass{};

        bool operator==(const Config&) const = default;
    };

    // Applies every recognised key in `in` on top of `cfg`. Unknown keys and bad values are
    // ignored, so a damaged file keeps the previous values.
    void ApplyIni(std::istream& in, Config& cfg);

    // Built-in defaults, then the shipped settings.ini, then the user's MCM file.
    // Missing files are fine.
    [[nodiscard]] Config LoadConfig(const std::filesystem::path& defaults, const std::filesystem::path& user);
}
