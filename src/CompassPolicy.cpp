#include "CompassPolicy.h"

namespace OnYourOwn
{
    CompassMarkerKind ClassifyCompassMarker(std::string_view a_className, std::string_view a_frameLabel) noexcept
    {
        if (a_className == "DirectionMarkerWidget") return CompassMarkerKind::Direction;
        if (a_className != "CompassMarkerWidget" && a_className != "FloatingTarget") return CompassMarkerKind::Unknown;

        if (a_frameLabel == "Quest") return CompassMarkerKind::Quest;
        if (a_frameLabel == "QuestDoor") return CompassMarkerKind::QuestDoor;
        if (a_frameLabel == "PlayerSet") return CompassMarkerKind::CustomWaypoint;
        if (a_frameLabel == "Recon") return CompassMarkerKind::Recon;
        if (a_frameLabel == "Enemy" || a_frameLabel == "EnemyTargeted") return CompassMarkerKind::Enemy;
        if (a_frameLabel == "Location") return CompassMarkerKind::Location;
        return CompassMarkerKind::Unknown;
    }

    std::optional<bool> CompassLocationDiscovered(std::string_view a_iconLabel) noexcept
    {
        if (a_iconLabel == "Discovered") return true;
        if (a_iconLabel == "Undiscovered") return false;
        return std::nullopt;
    }

    bool ShouldShowCompassMarker(CompassMarkerKind a_kind, std::optional<bool> a_discovered, const CompassSettings& a_settings) noexcept
    {
        switch (a_kind) {
        case CompassMarkerKind::Quest:
        case CompassMarkerKind::QuestDoor:
            return a_settings.showQuests;
        case CompassMarkerKind::CustomWaypoint:
            return a_settings.showCustomWaypoint;
        case CompassMarkerKind::Enemy:
            return a_settings.showEnemies;
        case CompassMarkerKind::Direction:
            return a_settings.showDirections;
        case CompassMarkerKind::Location:
            if (!a_discovered) return true;
            return *a_discovered ? a_settings.showDiscoveredLocations : a_settings.showUndiscoveredLocations;
        default:
            return true;  // unknown and recon
        }
    }

    bool ShouldShowFloatingMarker(CompassMarkerKind a_kind, std::optional<bool> a_discovered, const CompassSettings& a_settings) noexcept
    {
        if (a_kind == CompassMarkerKind::Quest || a_kind == CompassMarkerKind::QuestDoor) return a_settings.showFloatingQuests;
        return ShouldShowCompassMarker(a_kind, a_discovered, a_settings);
    }
}
