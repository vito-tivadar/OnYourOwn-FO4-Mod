#include "MarkerPolicy.h"

namespace OnYourOwn
{
    bool VisibleOn(MapVisibility visibility, MapContext map) noexcept
    {
        switch (visibility) {
        case MapVisibility::Always:
            return true;
        case MapVisibility::WorldOnly:
            return map == MapContext::World;
        case MapVisibility::LocalOnly:
            return map == MapContext::Local;
        case MapVisibility::Never:
            return false;
        default:
            return true;  // out-of-range value
        }
    }

    bool ShouldShowMarker(const MarkerInfo& marker, const Settings& settings) noexcept
    {
        const auto on = [&](MapVisibility v) { return VisibleOn(v, marker.map); };

        switch (marker.kind) {
        case MarkerKind::QuestObjective: {
            // Unknown group: hide only if both groups are hidden on this map.
            const auto groups = marker.questGroups ? marker.questGroups : (kQuestGroupQuests | kQuestGroupMisc);
            return ((groups & kQuestGroupQuests) && on(settings.questObjectives)) ||
                ((groups & kQuestGroupMisc) && on(settings.miscObjectives));
        }
        case MarkerKind::QuestDoor:
            return on(settings.questDoors);
        case MarkerKind::Player:
            return on(settings.player);
        case MarkerKind::CustomWaypoint:
            return on(settings.customWaypoint);
        case MarkerKind::PowerArmor:
            return on(settings.powerArmor);
        default:
            break;
        }

        // The local map has no location markers of its own.
        if (marker.map == MapContext::Local) return true;

        switch (marker.kind) {
        case MarkerKind::Settlement:
            return marker.discovered.value_or(false) && settings.showDiscoveredSettlements;
        case MarkerKind::Location:
            if (!marker.discovered) return true;
            return *marker.discovered ? settings.showDiscoveredLocations : settings.showUndiscoveredLocations;
        default:
            return true;
        }
    }
}
