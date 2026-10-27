#include "MarkerPolicy.h"

#include <cstdlib>
#include <iostream>

using namespace OnYourOwn;

namespace
{
void Check(bool actual, bool expected, const char* name)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool Show(MapContext map, MarkerKind kind, const Settings& s, std::uint8_t groups = 0)
{
    return ShouldShowMarker(MarkerInfo{ map, kind, std::nullopt, groups }, s);
}
}

int main()
{
    constexpr auto W = MapContext::World;
    constexpr auto L = MapContext::Local;
    const Settings defaults;

    // Exploration preset
    Check(ShouldShowMarker({W, MarkerKind::Location, false}, defaults), false, "undiscovered world location");
    Check(ShouldShowMarker({W, MarkerKind::Location, true}, defaults), true, "discovered world location");
    Check(ShouldShowMarker({W, MarkerKind::Settlement, false}, defaults), false, "undiscovered settlement");
    Check(ShouldShowMarker({W, MarkerKind::Settlement, true}, defaults), true, "discovered settlement");
    Check(Show(W, MarkerKind::QuestObjective, defaults), false, "world quest");
    Check(Show(L, MarkerKind::QuestObjective, defaults), false, "local quest");
    Check(Show(W, MarkerKind::QuestDoor, defaults), false, "world quest door");
    Check(Show(L, MarkerKind::QuestDoor, defaults), false, "local quest door");
    Check(Show(W, MarkerKind::Player, defaults), true, "world player");
    Check(Show(L, MarkerKind::Player, defaults), false, "local player");
    Check(Show(W, MarkerKind::CustomWaypoint, defaults), true, "world waypoint");
    Check(Show(L, MarkerKind::CustomWaypoint, defaults), false, "local waypoint");
    Check(Show(W, MarkerKind::PowerArmor, defaults), true, "world power armor");
    Check(Show(L, MarkerKind::PowerArmor, defaults), false, "local power armor");
    Check(Show(W, MarkerKind::Unknown, defaults), true, "unknown kind");
    Check(ShouldShowMarker({W, MarkerKind::Location}, defaults), true, "missing discovery state");
    Check(ShouldShowMarker({W, MarkerKind::Settlement}, defaults), false, "missing settlement discovery state");

    // Map choice
    Check(VisibleOn(MapVisibility::Always, W) && VisibleOn(MapVisibility::Always, L), true, "always");
    Check(VisibleOn(MapVisibility::WorldOnly, W) && !VisibleOn(MapVisibility::WorldOnly, L), true, "world only");
    Check(!VisibleOn(MapVisibility::LocalOnly, W) && VisibleOn(MapVisibility::LocalOnly, L), true, "local only");
    Check(VisibleOn(MapVisibility::Never, W) || VisibleOn(MapVisibility::Never, L), false, "never");
    Check(VisibleOn(static_cast<MapVisibility>(7), L), true, "out-of-range fails open");

    // Power armor obeys its setting on the local map (regression: was always shown)
    Settings s;
    s.powerArmor = MapVisibility::Never;
    Check(Show(L, MarkerKind::PowerArmor, s), false, "local power armor hidden");
    Check(Show(W, MarkerKind::PowerArmor, s), false, "world power armor hidden");
    s.powerArmor = MapVisibility::WorldOnly;
    Check(Show(W, MarkerKind::PowerArmor, s), true, "power armor world only: world");
    Check(Show(L, MarkerKind::PowerArmor, s), false, "power armor world only: local");

    // Player / waypoint per map
    s = {};
    s.player = MapVisibility::LocalOnly;
    s.customWaypoint = MapVisibility::WorldOnly;
    Check(Show(W, MarkerKind::Player, s), false, "player local only: world");
    Check(Show(L, MarkerKind::Player, s), true, "player local only: local");
    Check(Show(W, MarkerKind::CustomWaypoint, s), true, "waypoint world only: world");
    Check(Show(L, MarkerKind::CustomWaypoint, s), false, "waypoint world only: local");

    // Quest groups
    s = {};
    s.questObjectives = MapVisibility::Always;
    s.miscObjectives = MapVisibility::Never;
    Check(Show(W, MarkerKind::QuestObjective, s, kQuestGroupQuests), true, "quest group shown");
    Check(Show(W, MarkerKind::QuestObjective, s, kQuestGroupMisc), false, "misc group hidden");
    Check(Show(W, MarkerKind::QuestObjective, s, kQuestGroupQuests | kQuestGroupMisc), true, "shared marker: any group shown");
    Check(Show(W, MarkerKind::QuestObjective, s), true, "unknown group: shown if either group is shown");
    s.questObjectives = MapVisibility::Never;
    Check(Show(W, MarkerKind::QuestObjective, s), false, "unknown group: hidden when both hidden");
    s.miscObjectives = MapVisibility::LocalOnly;
    Check(Show(L, MarkerKind::QuestObjective, s, kQuestGroupMisc), true, "misc local only: local");
    Check(Show(W, MarkerKind::QuestObjective, s, kQuestGroupMisc), false, "misc local only: world");

    // Quest doors have their own setting, independent of objectives
    s = {};
    s.questObjectives = MapVisibility::Always;
    s.miscObjectives = MapVisibility::Always;
    s.questDoors = MapVisibility::Never;
    Check(Show(W, MarkerKind::QuestDoor, s), false, "quest door independent of objectives");
    s.questDoors = MapVisibility::LocalOnly;
    Check(Show(L, MarkerKind::QuestDoor, s), true, "quest door local only: local");
    s.questObjectives = MapVisibility::Never;
    Check(Show(W, MarkerKind::QuestObjective, s, kQuestGroupQuests), false, "objective independent of door");

    // Locations are world-only categories; local location kinds stay visible
    Settings changed;
    changed.showUndiscoveredLocations = true;
    changed.showDiscoveredLocations = false;
    changed.showDiscoveredSettlements = false;
    Check(ShouldShowMarker({W, MarkerKind::Location, false}, changed), true, "world undiscovered override");
    Check(ShouldShowMarker({W, MarkerKind::Location, true}, changed), false, "world discovered override");
    Check(ShouldShowMarker({W, MarkerKind::Settlement, true}, changed), false, "world settlement override");
    Check(ShouldShowMarker({L, MarkerKind::Location, true}, changed), true, "local location unaffected");

    std::cout << "Marker policy tests passed\n";
    return EXIT_SUCCESS;
}
