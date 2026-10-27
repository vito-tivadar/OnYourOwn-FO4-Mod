// Tests for marker classification, HUD policy and settings parsing. No game needed.
#include "IniSettings.h"
#include "CompassPolicy.h"
#include "MarkerClassifier.h"

#include <cstdio>
#include <sstream>

using namespace OnYourOwn;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static bool Show(MapContext m, std::uint32_t type, bool discovered, const Settings& s)
{
    return ShouldShowMarker(ClassifyMapMarker(m, type, discovered), s);
}

int main()
{
    namespace T = MarkerType;
    const Settings d{};

    // --- classification
    CHECK(ClassifyMapMarker(MapContext::World, T::kQuest, false).kind == MarkerKind::QuestObjective);
    CHECK(ClassifyMapMarker(MapContext::World, T::kQuestMultiple, false).kind == MarkerKind::QuestObjective);
    CHECK(ClassifyMapMarker(MapContext::World, T::kQuestDoor, false).kind == MarkerKind::QuestDoor);
    CHECK(ClassifyMapMarker(MapContext::World, T::kPlayerLocation, false).kind == MarkerKind::Player);
    CHECK(ClassifyMapMarker(MapContext::World, T::kPlayerSet, false).kind == MarkerKind::CustomWaypoint);
    CHECK(ClassifyMapMarker(MapContext::World, T::kPowerArmorLocation, false).kind == MarkerKind::PowerArmor);
    CHECK(ClassifyMapMarker(MapContext::World, T::kSettlement, true).kind == MarkerKind::Settlement);
    CHECK(ClassifyMapMarker(MapContext::World, 0x00, true).kind == MarkerKind::Location);   // cave
    CHECK(ClassifyMapMarker(MapContext::World, 0x0F, false).discovered == std::optional<bool>(false));
    CHECK(ClassifyMapMarker(MapContext::World, 0x0F, true).discovered == std::optional<bool>(true));
    // unrecognised -> Unknown
    CHECK(ClassifyMapMarker(MapContext::World, T::kDoor, true).kind == MarkerKind::Unknown);
    CHECK(ClassifyMapMarker(MapContext::World, 0x59, true).kind == MarkerKind::Unknown);     // dogmeat
    CHECK(ClassifyMapMarker(MapContext::World, 0x30, true).kind == MarkerKind::Unknown);
    CHECK(ClassifyMapMarker(MapContext::World, 0xFFFF, false).kind == MarkerKind::Unknown);

    // --- Flash clip classes
    CHECK(FlashClassName("[class QuestMarker]") == "QuestMarker");
    CHECK(FlashClassName("QuestMarkerDoor") == "QuestMarkerDoor");
    CHECK(FlashClassName("[class ") == "[class ");
    CHECK(ClassifyFlashMarkerClass(MapContext::World, "QuestMarker").kind == MarkerKind::QuestObjective);
    CHECK(ClassifyFlashMarkerClass(MapContext::World, "QuestMarkerDoor").kind == MarkerKind::QuestDoor);
    CHECK(ClassifyFlashMarkerClass(MapContext::Local, "QuestMarkerDoor").kind == MarkerKind::QuestDoor);
    CHECK(ClassifyFlashMarkerClass(MapContext::Local, "PlayerLocMarker").kind == MarkerKind::Player);
    CHECK(ClassifyFlashMarkerClass(MapContext::World, "PlayerSetMarker").kind == MarkerKind::Unknown);  // tree-handled
    CHECK(ClassifyFlashMarkerClass(MapContext::World, "CaveMarker").kind == MarkerKind::Unknown);
    CHECK(ClassifyFlashMarkerClass(MapContext::World, "").kind == MarkerKind::Unknown);
    CHECK(VanillaFlashMarkerClass(0x52) == "QuestMarker");
    CHECK(VanillaFlashMarkerClass(0x53) == "QuestMarkerDoor");
    CHECK(VanillaFlashMarkerClass(0x54) == "QuestMarker");
    CHECK(VanillaFlashMarkerClass(0x56) == "PlayerLocMarker");
    CHECK(VanillaFlashMarkerClass(0x55).empty());  // custom waypoint: tree-handled
    CHECK(VanillaFlashMarkerClass(0x0C).empty());
    {   // quest objective and quest door settings are independent
        Settings q{}; q.questObjectives = q.miscObjectives = MapVisibility::Always; q.questDoors = MapVisibility::WorldOnly;
        CHECK(ShouldShowMarker(ClassifyFlashMarkerClass(MapContext::World, "QuestMarker"), q));
        CHECK(ShouldShowMarker(ClassifyFlashMarkerClass(MapContext::World, "QuestMarkerDoor"), q));
        CHECK(!ShouldShowMarker(ClassifyFlashMarkerClass(MapContext::Local, "QuestMarkerDoor"), q));
    }
    // quest types (RE::QUEST_TYPE) -> Pip-Boy groups
    CHECK(QuestGroupForType(1) == kQuestGroupQuests);   // main quest
    CHECK(QuestGroupForType(4) == kQuestGroupQuests);   // Minutemen
    CHECK(QuestGroupForType(7) == kQuestGroupQuests);   // side quests
    CHECK(QuestGroupForType(10) == kQuestGroupQuests);  // DLC03
    CHECK(QuestGroupForType(6) == kQuestGroupMisc);     // miscellaneous
    CHECK(QuestGroupForType(0) == kQuestGroupMisc);     // none

    // --- fail open: unknown always visible, every setting off
    Settings off{};
    off.showUndiscoveredLocations = off.showDiscoveredLocations = off.showDiscoveredSettlements = false;
    off.questObjectives = off.miscObjectives = off.questDoors = MapVisibility::Never;
    off.player = off.customWaypoint = off.powerArmor = MapVisibility::Never;
    CHECK(Show(MapContext::World, T::kDoor, true, off));
    CHECK(Show(MapContext::World, 0xFFFF, false, off));
    CHECK(Show(MapContext::Local, T::kDoor, true, off));

    // --- settlements: undiscovered never shown, even with every option on
    Settings on{};
    on.showUndiscoveredLocations = on.showDiscoveredLocations = on.showDiscoveredSettlements = true;
    CHECK(!Show(MapContext::World, T::kSettlement, false, on));
    CHECK(Show(MapContext::World, T::kSettlement, true, on));
    CHECK(!Show(MapContext::World, T::kSettlement, true, off));
    // undiscovered-locations option does not leak into settlements
    Settings undiscOnly{}; undiscOnly.showUndiscoveredLocations = true; undiscOnly.showDiscoveredSettlements = false;
    CHECK(!Show(MapContext::World, T::kSettlement, false, undiscOnly));

    // --- locations
    CHECK(!Show(MapContext::World, 0x0F, false, d));   // undiscovered hidden by default
    CHECK(Show(MapContext::World, 0x0F, true, d));    // discovered shown by default

    // --- world/local independence
    Settings s{}; s.player = MapVisibility::LocalOnly;
    CHECK(!Show(MapContext::World, T::kPlayerLocation, false, s));
    CHECK(Show(MapContext::Local, T::kPlayerLocation, false, s));
    s = {}; s.questObjectives = s.miscObjectives = MapVisibility::WorldOnly;
    CHECK(Show(MapContext::World, T::kQuest, false, s));
    CHECK(!Show(MapContext::Local, T::kQuest, false, s));
    s = {}; s.customWaypoint = MapVisibility::WorldOnly;
    CHECK(Show(MapContext::World, T::kPlayerSet, false, s));
    CHECK(!Show(MapContext::Local, T::kPlayerSet, false, s));
    s = {}; s.powerArmor = MapVisibility::Never;
    CHECK(!Show(MapContext::Local, T::kPowerArmorLocation, false, s));  // regression: local PA was always shown

    // --- INI parsing
    {
        Config c;
        std::istringstream in("\xEF\xBB\xBF; comment\n[Locations]\nbShowUndiscovered=1\n[Markers]\niPlayer = 2\n"
                              "iCustomWaypoint=banana\niPowerArmor=4\n[Quests]\niMiscObjectives=1\niQuestDoors=-1\n"
                              "[Nope]\niPlayer=3\n[World]\nbShowPlayer=0\n[general]\nBENABLED=off\n");
        ApplyIni(in, c);
        CHECK(c.markers.showUndiscoveredLocations);
        CHECK(c.markers.player == MapVisibility::LocalOnly);
        CHECK(c.markers.customWaypoint == MapVisibility::WorldOnly); // bad value ignored -> default
        CHECK(c.markers.powerArmor == MapVisibility::WorldOnly);   // out of range ignored
        CHECK(c.markers.miscObjectives == MapVisibility::WorldOnly);
        CHECK(c.markers.questDoors == MapVisibility::Never);       // negative ignored -> default
        CHECK(c.markers.questObjectives == MapVisibility::Never);  // untouched default
        CHECK(!c.enabled);                                         // case-insensitive section/key
    }
    {   // log level dropdown
        Config c;
        std::istringstream in("[General]\niLogLevel=3\n");
        ApplyIni(in, c);
        CHECK(c.logLevel == LogLevel::Quests);
        std::istringstream bad("[General]\niLogLevel=7\nbLogMarkers=1\n");
        ApplyIni(bad, c);
        CHECK(c.logLevel == LogLevel::Quests);  // out of range and old key ignored
    }
    // log categories
    CHECK(!ShouldLog(LogLevel::None, LogCategory::Locations));
    CHECK(!ShouldLog(LogLevel::None, LogCategory::MapView));
    CHECK(ShouldLog(LogLevel::Everything, LogCategory::Quests));
    CHECK(ShouldLog(LogLevel::Everything, LogCategory::MapView));
    CHECK(ShouldLog(LogLevel::Locations, LogCategory::Locations));
    CHECK(!ShouldLog(LogLevel::Locations, LogCategory::Quests));
    CHECK(ShouldLog(LogLevel::Quests, LogCategory::Quests));
    CHECK(ShouldLog(LogLevel::MarkerHiding, LogCategory::MarkerHiding));
    CHECK(!ShouldLog(LogLevel::MarkerHiding, LogCategory::MapView));
    CHECK(ShouldLog(LogLevel::MapView, LogCategory::MapView));
    CHECK(ShouldLog(LogLevel::Compass, LogCategory::Compass));
    CHECK(!ShouldLog(LogLevel::Compass, LogCategory::MapView));
    CHECK(!ShouldLog(LogLevel::MapView, LogCategory::Compass));
    CHECK(ShouldLog(LogLevel::Everything, LogCategory::Compass));
    {
        Config c;
        std::istringstream in("[General]\niLogLevel=6\n");
        ApplyIni(in, c);
        CHECK(c.logLevel == LogLevel::Compass);
    }
    CHECK(!ShouldLog(static_cast<LogLevel>(9), LogCategory::Quests));
    // --- compass
    {
        using K = CompassMarkerKind;
        CHECK(ClassifyCompassMarker("CompassMarkerWidget", "Quest") == K::Quest);
        CHECK(ClassifyCompassMarker("CompassMarkerWidget", "QuestDoor") == K::QuestDoor);
        CHECK(ClassifyCompassMarker("CompassMarkerWidget", "PlayerSet") == K::CustomWaypoint);
        CHECK(ClassifyCompassMarker("CompassMarkerWidget", "Recon") == K::Recon);
        CHECK(ClassifyCompassMarker("CompassMarkerWidget", "Enemy") == K::Enemy);
        CHECK(ClassifyCompassMarker("CompassMarkerWidget", "EnemyTargeted") == K::Enemy);
        CHECK(ClassifyCompassMarker("CompassMarkerWidget", "Location") == K::Location);
        CHECK(ClassifyCompassMarker("CompassMarkerWidget", "Somethingelse") == K::Unknown);
        CHECK(ClassifyCompassMarker("DirectionMarkerWidget", "") == K::Direction);
        CHECK(ClassifyCompassMarker("MovieClip", "Quest") == K::Unknown);
        CHECK(ClassifyCompassMarker("FloatingTarget", "QuestDoor") == K::QuestDoor);
        CHECK(ClassifyCompassMarker("FloatingTarget", "PlayerSet") == K::CustomWaypoint);
        CHECK(ClassifyCompassMarker("FloatingTarget", "Enemy") == K::Enemy);
        CHECK(CompassLocationDiscovered("Discovered") == std::optional<bool>{ true });
        CHECK(CompassLocationDiscovered("Undiscovered") == std::optional<bool>{ false });
        CHECK(!CompassLocationDiscovered("LevelWithPlayer"));

        const CompassSettings all{ true, true, true, true, true, true, true, true };
        CHECK(all.AllShown());
        const CompassSettings shipped{};  // Exploration preset
        CHECK(!shipped.AllShown());
        CHECK(!ShouldShowCompassMarker(K::Quest, std::nullopt, shipped));
        CHECK(!ShouldShowCompassMarker(K::Enemy, std::nullopt, shipped));
        CHECK(!ShouldShowCompassMarker(K::Location, false, shipped));
        CHECK(ShouldShowCompassMarker(K::Location, true, shipped));
        CHECK(ShouldShowCompassMarker(K::CustomWaypoint, std::nullopt, shipped));
        CHECK(!ShouldShowFloatingMarker(K::QuestDoor, std::nullopt, shipped));
        for (auto k : { K::Unknown, K::Quest, K::QuestDoor, K::CustomWaypoint, K::Recon, K::Enemy, K::Location, K::Direction }) {
            CHECK(ShouldShowCompassMarker(k, std::nullopt, all));
        }
        CompassSettings c = all;
        c.showQuests = false;
        c.showEnemies = false;
        c.showUndiscoveredLocations = false;
        c.showCustomWaypoint = false;
        c.showDirections = false;
        CHECK(!c.AllShown());
        CHECK(!ShouldShowCompassMarker(K::Quest, std::nullopt, c));
        CHECK(!ShouldShowCompassMarker(K::QuestDoor, std::nullopt, c));
        CHECK(!ShouldShowCompassMarker(K::Enemy, std::nullopt, c));
        CHECK(!ShouldShowCompassMarker(K::CustomWaypoint, std::nullopt, c));
        CHECK(!ShouldShowCompassMarker(K::Direction, std::nullopt, c));
        CHECK(!ShouldShowCompassMarker(K::Location, false, c));
        CHECK(ShouldShowCompassMarker(K::Location, true, c));
        CHECK(ShouldShowCompassMarker(K::Location, std::nullopt, c));  // unknown discovery: shown
        CHECK(ShouldShowCompassMarker(K::Recon, std::nullopt, c));     // recon: always shown
        CHECK(ShouldShowCompassMarker(K::Unknown, std::nullopt, c));
        c = all;
        c.showDiscoveredLocations = false;
        CHECK(!ShouldShowCompassMarker(K::Location, true, c));
        CHECK(ShouldShowCompassMarker(K::Location, false, c));
        c = all;
        c.showCompass = false;
        CHECK(!c.AllShown());

        Config cfg;
        std::istringstream in("[Compass]\nbShowCompass=0\nbShowEnemies=0\nbShowDiscovered=0\nbShowQuests=1\nbShowUndiscovered=on\n");
        ApplyIni(in, cfg);
        CHECK(!cfg.compass.showCompass && !cfg.compass.showEnemies && !cfg.compass.showDiscoveredLocations);
        CHECK(cfg.compass.showQuests && cfg.compass.showUndiscoveredLocations);
        CHECK(cfg.markers.showDiscoveredLocations);  // map setting untouched

        // floating markers: quests have their own switch, the rest follow the compass
        CompassSettings f = all;
        f.showQuests = false;
        CHECK(ShouldShowFloatingMarker(K::Quest, std::nullopt, f));
        CHECK(!ShouldShowCompassMarker(K::Quest, std::nullopt, f));
        f = all;
        f.showFloatingQuests = false;
        CHECK(!f.AllShown());
        CHECK(!ShouldShowFloatingMarker(K::Quest, std::nullopt, f));
        CHECK(!ShouldShowFloatingMarker(K::QuestDoor, std::nullopt, f));
        CHECK(ShouldShowCompassMarker(K::QuestDoor, std::nullopt, f));
        f.showEnemies = false;
        CHECK(!ShouldShowFloatingMarker(K::Enemy, std::nullopt, f));
        CHECK(ShouldShowFloatingMarker(K::Recon, std::nullopt, f));
        Config q;
        std::istringstream qin("[Quests]\nbShowFloatingMarkers=1\n");
        ApplyIni(qin, q);
        CHECK(q.compass.showFloatingQuests && !q.compass.showQuests);
    }
    {   // layering: user overrides defaults file; missing files are fine
        Config c = LoadConfig("does/not/exist.ini", "also/missing.ini");
        CHECK(c.enabled && c.logLevel == LogLevel::None);
        CHECK(c.markers.showDiscoveredLocations && c.markers.questObjectives == MapVisibility::Never);
    }

    std::printf(g_fail ? "%d FAILED\n" : "all tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
