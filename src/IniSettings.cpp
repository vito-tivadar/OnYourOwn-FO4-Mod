#include "IniSettings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <istream>
#include <optional>
#include <string>
#include <string_view>

namespace OnYourOwn
{
    namespace
    {
        struct Binding
        {
            std::string_view section;
            std::string_view key;
            // Exactly one of these is set.
            bool& (*flag)(Config&);
            MapVisibility& (*visibility)(Config&);
            LogLevel& (*logLevel)(Config&);
        };

        constexpr Binding Flag(std::string_view a_section, std::string_view a_key, bool& (*a_get)(Config&))
        {
            return { a_section, a_key, a_get, nullptr, nullptr };
        }

        constexpr Binding Choice(std::string_view a_section, std::string_view a_key, MapVisibility& (*a_get)(Config&))
        {
            return { a_section, a_key, nullptr, a_get, nullptr };
        }

        constexpr Binding Log(std::string_view a_section, std::string_view a_key, LogLevel& (*a_get)(Config&))
        {
            return { a_section, a_key, nullptr, nullptr, a_get };
        }

        // Section/key names must match Data/MCM/Config/OnYourOwn/config.json ids ("key:Section").
        constexpr std::array<Binding, 19> kBindings{ {
            Flag("General", "bEnabled", [](Config& c) -> bool& { return c.enabled; }),
            Log("General", "iLogLevel", [](Config& c) -> LogLevel& { return c.logLevel; }),

            Flag("Locations", "bShowUndiscovered", [](Config& c) -> bool& { return c.markers.showUndiscoveredLocations; }),
            Flag("Locations", "bShowDiscovered", [](Config& c) -> bool& { return c.markers.showDiscoveredLocations; }),
            Flag("Locations", "bShowDiscoveredSettlements", [](Config& c) -> bool& { return c.markers.showDiscoveredSettlements; }),

            Choice("Quests", "iQuestObjectives", [](Config& c) -> MapVisibility& { return c.markers.questObjectives; }),
            Choice("Quests", "iMiscObjectives", [](Config& c) -> MapVisibility& { return c.markers.miscObjectives; }),
            Choice("Quests", "iQuestDoors", [](Config& c) -> MapVisibility& { return c.markers.questDoors; }),
            Flag("Quests", "bShowFloatingMarkers", [](Config& c) -> bool& { return c.compass.showFloatingQuests; }),

            Choice("Markers", "iPlayer", [](Config& c) -> MapVisibility& { return c.markers.player; }),
            Choice("Markers", "iCustomWaypoint", [](Config& c) -> MapVisibility& { return c.markers.customWaypoint; }),
            Choice("Markers", "iPowerArmor", [](Config& c) -> MapVisibility& { return c.markers.powerArmor; }),

            Flag("Compass", "bShowCompass", [](Config& c) -> bool& { return c.compass.showCompass; }),
            Flag("Compass", "bShowQuests", [](Config& c) -> bool& { return c.compass.showQuests; }),
            Flag("Compass", "bShowEnemies", [](Config& c) -> bool& { return c.compass.showEnemies; }),
            Flag("Compass", "bShowUndiscovered", [](Config& c) -> bool& { return c.compass.showUndiscoveredLocations; }),
            Flag("Compass", "bShowDiscovered", [](Config& c) -> bool& { return c.compass.showDiscoveredLocations; }),
            Flag("Compass", "bShowCustomWaypoint", [](Config& c) -> bool& { return c.compass.showCustomWaypoint; }),
            Flag("Compass", "bShowDirections", [](Config& c) -> bool& { return c.compass.showDirections; }),
        } };

        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
            return s;
        }

        bool IEquals(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() &&
                std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                    return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                });
        }

        std::optional<bool> ParseBool(std::string_view v)
        {
            if (v == "1" || IEquals(v, "true") || IEquals(v, "yes") || IEquals(v, "on")) return true;
            if (v == "0" || IEquals(v, "false") || IEquals(v, "no") || IEquals(v, "off")) return false;
            return std::nullopt;
        }

        constexpr auto kMaxVisibility = static_cast<std::int32_t>(MapVisibility::Never);
        constexpr auto kMaxLogLevel = static_cast<std::int32_t>(LogLevel::Compass);

        // Single-digit dropdown index in [0, a_max].
        std::optional<std::int32_t> ParseIndex(std::string_view v, std::int32_t a_max)
        {
            if (v.size() != 1 || v[0] < '0' || v[0] > '0' + a_max) return std::nullopt;
            return v[0] - '0';
        }
    }

    bool ShouldLog(LogLevel level, LogCategory category) noexcept
    {
        switch (level) {
        case LogLevel::Everything:
            return true;
        case LogLevel::Locations:
            return category == LogCategory::Locations;
        case LogLevel::Quests:
            return category == LogCategory::Quests;
        case LogLevel::MarkerHiding:
            return category == LogCategory::MarkerHiding;
        case LogLevel::MapView:
            return category == LogCategory::MapView;
        case LogLevel::Compass:
            return category == LogCategory::Compass;
        default:
            return false;
        }
    }

    void ApplyIni(std::istream& in, Config& cfg)
    {
        std::string line;
        std::string section;
        bool        first = true;

        while (std::getline(in, line)) {
            std::string_view sv{ line };
            if (first) {
                first = false;
                if (sv.size() >= 3 && sv.substr(0, 3) == "\xEF\xBB\xBF") sv.remove_prefix(3);  // UTF-8 BOM
            }
            sv = Trim(sv);
            if (sv.empty() || sv.front() == ';' || sv.front() == '#') continue;

            if (sv.front() == '[') {
                const auto close = sv.find(']');
                section = (close == std::string_view::npos) ? std::string{} : std::string{ Trim(sv.substr(1, close - 1)) };
                continue;
            }

            const auto eq = sv.find('=');
            if (eq == std::string_view::npos) continue;
            const auto key = Trim(sv.substr(0, eq));
            const auto value = Trim(sv.substr(eq + 1));

            for (const auto& b : kBindings) {
                if (!IEquals(b.section, section) || !IEquals(b.key, key)) continue;
                // Unparsable or out-of-range values keep the existing value.
                if (b.flag) {
                    if (const auto v = ParseBool(value)) b.flag(cfg) = *v;
                } else if (b.visibility) {
                    if (const auto v = ParseIndex(value, kMaxVisibility)) b.visibility(cfg) = static_cast<MapVisibility>(*v);
                } else if (const auto v = ParseIndex(value, kMaxLogLevel)) {
                    b.logLevel(cfg) = static_cast<LogLevel>(*v);
                }
                break;
            }
        }
    }

    Config LoadConfig(const std::filesystem::path& defaults, const std::filesystem::path& user)
    {
        Config cfg{};
        for (const auto* p : { &defaults, &user }) {
            std::ifstream f{ *p };
            if (f) ApplyIni(f, cfg);
        }
        return cfg;
    }
}
