#pragma once

#include <cstdint>
#include <optional>

namespace OnYourOwn
{
    enum class MapContext
    {
        World,
        Local
    };

    enum class MarkerKind
    {
        Unknown,
        Location,
        Settlement,
        QuestObjective,
        QuestDoor,
        Player,
        CustomWaypoint,
        PowerArmor
    };

    // Quest groups as in the Pip-Boy quest list. A bit mask, because one marker can belong to
    // quests of both groups.
    inline constexpr std::uint8_t kQuestGroupQuests = 0x1;  // main, faction, side and DLC quests
    inline constexpr std::uint8_t kQuestGroupMisc = 0x2;    // miscellaneous quests

    struct MarkerInfo
    {
        MapContext          map;
        MarkerKind          kind;
        std::optional<bool> discovered = std::nullopt;  // locations and settlements only
        std::uint8_t        questGroups = 0;            // quest objectives only; 0 = unknown
    };

    // Which map(s) a marker kind shows on. Values are the MCM dropdown indices; do not reorder.
    enum class MapVisibility : std::int32_t
    {
        Always = 0,
        WorldOnly = 1,
        LocalOnly = 2,
        Never = 3
    };

    [[nodiscard]] bool VisibleOn(MapVisibility visibility, MapContext map) noexcept;

    // Pip-Boy map settings. The defaults are the shipped preset (settings.ini must match).
    struct Settings
    {
        // Locations exist on the world map only.
        bool showUndiscoveredLocations = false;
        bool showDiscoveredLocations = true;
        bool showDiscoveredSettlements = true;

        MapVisibility questObjectives = MapVisibility::Never;
        MapVisibility miscObjectives = MapVisibility::Never;
        MapVisibility questDoors = MapVisibility::Never;

        MapVisibility player = MapVisibility::WorldOnly;
        MapVisibility customWaypoint = MapVisibility::WorldOnly;
        MapVisibility powerArmor = MapVisibility::WorldOnly;

        bool operator==(const Settings&) const = default;
    };

    // Unknown kinds always stay visible. Undiscovered settlements are never shown.
    [[nodiscard]] bool ShouldShowMarker(const MarkerInfo& marker, const Settings& settings) noexcept;
}
