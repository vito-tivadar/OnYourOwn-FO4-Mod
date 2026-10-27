#pragma once

#include <optional>
#include <string_view>

namespace OnYourOwn
{
    // HUD marker categories, from the frame labels in HUDMenu.swf. Compass markers are
    // CompassMarkerWidget clips; floating in-world markers are FloatingTarget clips with the
    // same labels. The N/E/S/W letters are DirectionMarkerWidget clips.
    enum class CompassMarkerKind
    {
        Unknown,
        Quest,
        QuestDoor,
        CustomWaypoint,  // "PlayerSet"
        Recon,           // targets marked with a recon scope
        Enemy,           // "Enemy" and "EnemyTargeted"
        Location,
        Direction
    };

    // HUD settings. The defaults are the shipped preset (settings.ini must match).
    struct CompassSettings
    {
        bool showCompass = true;  // false hides the whole compass bar
        bool showQuests = false;  // quest objectives and quest doors on the compass
        bool showEnemies = false;
        bool showUndiscoveredLocations = false;
        bool showDiscoveredLocations = true;
        bool showCustomWaypoint = true;
        bool showDirections = true;       // N/E/S/W
        bool showFloatingQuests = false;  // quest markers floating in the world (MCM Quests page)

        // With everything shown the HUD is not touched at all.
        [[nodiscard]] bool AllShown() const noexcept
        {
            return showCompass && showQuests && showEnemies && showUndiscoveredLocations && showDiscoveredLocations &&
                showCustomWaypoint && showDirections && showFloatingQuests;
        }

        bool operator==(const CompassSettings&) const = default;
    };

    // a_className: AS3 class of the marker clip; a_frameLabel: its current frame label.
    [[nodiscard]] CompassMarkerKind ClassifyCompassMarker(std::string_view a_className, std::string_view a_frameLabel) noexcept;

    // Location icon frame label ("Discovered" / "Undiscovered") -> discovered state.
    [[nodiscard]] std::optional<bool> CompassLocationDiscovered(std::string_view a_iconLabel) noexcept;

    // Compass marker. Unknown kinds, recon targets and locations of unknown state stay visible.
    [[nodiscard]] bool ShouldShowCompassMarker(CompassMarkerKind a_kind, std::optional<bool> a_discovered, const CompassSettings& a_settings) noexcept;

    // Floating marker: quests and quest doors follow showFloatingQuests, the rest as on the compass.
    // Not affected by showCompass.
    [[nodiscard]] bool ShouldShowFloatingMarker(CompassMarkerKind a_kind, std::optional<bool> a_discovered, const CompassSettings& a_settings) noexcept;
}
