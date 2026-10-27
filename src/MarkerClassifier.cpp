#include "MarkerClassifier.h"

namespace OnYourOwn
{
    MarkerInfo ClassifyMapMarker(MapContext map, std::uint32_t type, bool discovered) noexcept
    {
        using namespace MarkerType;

        switch (type) {
        case kQuest:
        case kQuestMultiple:
            return { map, MarkerKind::QuestObjective };
        case kQuestDoor:
            return { map, MarkerKind::QuestDoor };
        case kPlayerLocation:
            return { map, MarkerKind::Player };
        case kPlayerSet:
            return { map, MarkerKind::CustomWaypoint };
        case kPowerArmorLocation:
            return { map, MarkerKind::PowerArmor };
        case kSettlement:
        case kRaiderSettlement:
        case kVassalSettlement:
        case kPotentialVassalSettlement:
            return { map, MarkerKind::Settlement, discovered };
        default:
            break;
        }

        if (type <= kPack && type != kSynthHeadSlot) {
            return { map, MarkerKind::Location, discovered };
        }
        return { map, MarkerKind::Unknown };  // doors, Dogmeat, companions, future types
    }

    std::string_view FlashClassName(std::string_view classString) noexcept
    {
        constexpr std::string_view prefix = "[class ";
        if (classString.starts_with(prefix) && classString.ends_with(']')) {
            classString.remove_prefix(prefix.size());
            classString.remove_suffix(1);
        }
        return classString;
    }

    std::string_view VanillaFlashMarkerClass(std::uint32_t flashType) noexcept
    {
        switch (flashType) {
        case 0x52:
        case 0x54:
            return "QuestMarker";
        case 0x53:
            return "QuestMarkerDoor";
        case 0x56:
            return "PlayerLocMarker";
        default:
            return {};
        }
    }

    MarkerInfo ClassifyFlashMarkerClass(MapContext map, std::string_view className) noexcept
    {
        if (className == "QuestMarker") return { map, MarkerKind::QuestObjective };
        if (className == "QuestMarkerDoor") return { map, MarkerKind::QuestDoor };
        if (className == "PlayerLocMarker") return { map, MarkerKind::Player };
        return { map, MarkerKind::Unknown };
    }

    std::uint8_t QuestGroupForType(std::int32_t questType) noexcept
    {
        return (questType == 0 || questType == 6) ? kQuestGroupMisc : kQuestGroupQuests;
    }
}
