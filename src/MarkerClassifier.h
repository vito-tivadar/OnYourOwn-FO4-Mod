#pragma once

#include "MarkerPolicy.h"

#include <cstdint>
#include <string_view>

namespace OnYourOwn
{
    // Values of RE::MARKER_TYPE (CommonLibF4 RE/M/MARKER_TYPE.h), kept as plain numbers so this
    // file builds without the game headers. PipboyFilter.cpp static_asserts them against the enum.
    namespace MarkerType
    {
        inline constexpr std::uint32_t kSettlement = 0x0D;
        inline constexpr std::uint32_t kSynthHeadSlot = 0x30;  // the header's kSynthHead = 0x0 looks wrong; 0x30 is treated as unknown
        inline constexpr std::uint32_t kRaiderSettlement = 0x42;
        inline constexpr std::uint32_t kVassalSettlement = 0x43;
        inline constexpr std::uint32_t kPotentialVassalSettlement = 0x44;
        inline constexpr std::uint32_t kPack = 0x50;  // last location icon
        inline constexpr std::uint32_t kDoor = 0x51;
        inline constexpr std::uint32_t kQuest = 0x53;
        inline constexpr std::uint32_t kQuestDoor = 0x54;
        inline constexpr std::uint32_t kQuestMultiple = 0x55;
        inline constexpr std::uint32_t kPlayerSet = 0x56;
        inline constexpr std::uint32_t kPlayerLocation = 0x57;
        inline constexpr std::uint32_t kPowerArmorLocation = 0x58;
    }

    // Pip-Boy data tree marker (RE::MARKER_TYPE + discovered state) -> MarkerInfo.
    // Anything not recognised becomes MarkerKind::Unknown, which stays visible.
    [[nodiscard]] MarkerInfo ClassifyMapMarker(MapContext map, std::uint32_t type, bool discovered) noexcept;

    // "[class QuestMarker]" (AS3 Class.toString()) -> "QuestMarker".
    [[nodiscard]] std::string_view FlashClassName(std::string_view classString) noexcept;

    // Clip class the vanilla Pipboy_MapPage.swf creates for a SWF marker entry type (its
    // MarkerTypesA table; these types are one lower than RE::MARKER_TYPE). Only the classes
    // handled on the SWF side are listed; anything else returns an empty view.
    [[nodiscard]] std::string_view VanillaFlashMarkerClass(std::uint32_t flashType) noexcept;

    // SWF marker clip class -> MarkerInfo. Only quest, quest door and player clips are
    // recognised; everything else is Unknown.
    [[nodiscard]] MarkerInfo ClassifyFlashMarkerClass(MapContext map, std::string_view className) noexcept;

    // RE::QUEST_TYPE (TESQuest::data.questType) -> Pip-Boy quest group. Miscellaneous (6) and
    // None (0, radiant/untyped) are misc; main, faction, side and DLC quests are quests.
    [[nodiscard]] std::uint8_t QuestGroupForType(std::int32_t questType) noexcept;
}
