#include "PipboyFilter.h"

#include "HudCompass.h"
#include "IniSettings.h"
#include "Log.h"
#include "MarkerClassifier.h"
#include "MarkerPolicy.h"

#include <RE/Fallout.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// How the Pip-Boy map filter works (more detail in docs/how-it-works.md):
//
//  * PipboyMapMenu::UpdateData is hooked; the filter runs before and after the original.
//  * Tree pass: the Pip-Boy data tree (RE::PipboyMapData) has a "Visible" member on location,
//    custom waypoint and power armor markers. It is set per ShouldShowMarker(); values we change
//    are remembered and restored when the marker should show again.
//  * Flash pass: quest and player markers have no "Visible" member. The game rebuilds the SWF
//    marker arrays (dataObj.WorldMapMarkers / LocalMapMarkers) on every update, so hidden entries
//    are removed from the fresh array before the SWF creates clips for them. Nothing is drawn and
//    nothing can be hovered or selected.
//  * World map view: with the player marker hidden, the map is zoomed out and centered instead
//    of staying at the top-left corner. This runs every UI frame.
//
// Nothing persistent (discovery, quests, fast travel, map marker forms, saves) is ever written,
// and anything unrecognised is left as the game made it.

namespace OnYourOwn::Pipboy
{
    namespace
    {
        using GValue = ::Scaleform::GFx::Value;

        // MarkerClassifier keeps these as plain numbers so it can be unit-tested without the game.
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kSettlement) == MarkerType::kSettlement);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kRaiderSettlement) == MarkerType::kRaiderSettlement);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kVassalSettlement) == MarkerType::kVassalSettlement);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kPotentialVassalSettlement) == MarkerType::kPotentialVassalSettlement);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kPack) == MarkerType::kPack);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kDoor) == MarkerType::kDoor);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kQuest) == MarkerType::kQuest);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kQuestDoor) == MarkerType::kQuestDoor);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kQuestMultiple) == MarkerType::kQuestMultiple);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kPlayerSet) == MarkerType::kPlayerSet);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kPlayerLocation) == MarkerType::kPlayerLocation);
        static_assert(static_cast<std::uint32_t>(RE::MARKER_TYPE::kPowerArmorLocation) == MarkerType::kPowerArmorLocation);

        constexpr std::size_t   kUpdateDataVtableSlot = 2;  // PipboySubMenu: destructor, ProcessEvent, UpdateData
        constexpr std::uint32_t kMaxFlashMarkers = 4096;     // sanity limit for SWF array sizes

        Config         g_config{};
        std::uintptr_t g_originalUpdateData = 0;
        bool           g_installed = false;

        // Tree "Visible" values we changed, so they can be restored. The id guards against the
        // game replacing the member object.
        struct SavedVisible
        {
            std::uint32_t id;
            bool          original;
        };
        std::unordered_map<RE::PipboyObject*, SavedVisible> g_saved;

        // Results of the tree pass, keyed by PipboyValue::m_id. Flash entries carry the same id
        // in their `arrayIndex` member, which links the two passes.
        std::unordered_map<std::uint32_t, std::uint8_t> g_questGroups;   // quest marker -> quest groups
        std::unordered_map<std::uint32_t, bool>         g_locationShow;  // location -> show?

        std::unordered_map<std::uint32_t, std::string> g_flashClassByType;  // SWF marker type -> clip class

        // The PipboyMapMenu seen by the hook, used by the per-frame view code. Cleared when the
        // Pip-Boy closes. Menu events and map updates come from different threads, hence atomics.
        std::atomic<RE::PipboyMapMenu*> g_mapMenu{ nullptr };
        std::atomic<bool>               g_centerRequested{ false };
        bool                            g_viewDisabled = false;
        bool                            g_compassDisabled = false;

        // World map zoom/center state while the player marker is hidden.
        struct ViewState
        {
            bool                                                 centerPending = false;
            bool                                                 wasVisible = false;
            bool                                                 wasPageShown = false;
            std::optional<std::chrono::steady_clock::time_point> appliedAt;
        };
        ViewState g_worldView;

        // Diagnostics state, only used for logging.
        bool                              g_layoutChecked = false;
        bool                              g_layoutVerified = false;
        bool                              g_loggedLocations = false;
        bool                              g_loggedQuestTypes = false;
        bool                              g_dumpedFlashEntry = false;
        bool                              g_loggedFlashMissing = false;
        std::unordered_set<std::uint32_t> g_reportedLeaks;  // hidden locations already logged this open
        std::string                       g_lastTreeSummary, g_lastFlashSummary;

        bool LogOn(LogCategory a_category)
        {
            return ShouldLog(g_config.logLevel, a_category);
        }

        const char* KindName(MarkerKind a_kind)
        {
            switch (a_kind) {
            case MarkerKind::Location: return "location";
            case MarkerKind::Settlement: return "settlement";
            case MarkerKind::QuestObjective: return "quest_marker";
            case MarkerKind::QuestDoor: return "quest_door";
            case MarkerKind::Player: return "player";
            case MarkerKind::CustomWaypoint: return "custom_waypoint";
            case MarkerKind::PowerArmor: return "power_armor";
            default: return "unknown";
            }
        }

        const char* VisibilityName(MapVisibility a_visibility)
        {
            switch (a_visibility) {
            case MapVisibility::Always: return "always";
            case MapVisibility::WorldOnly: return "world";
            case MapVisibility::LocalOnly: return "local";
            case MapVisibility::Never: return "never";
            default: return "?";
            }
        }

        const char* LogLevelName(LogLevel a_level)
        {
            switch (a_level) {
            case LogLevel::None: return "none";
            case LogLevel::Everything: return "everything";
            case LogLevel::Locations: return "locations";
            case LogLevel::Quests: return "quests";
            case LogLevel::MarkerHiding: return "marker_hiding";
            case LogLevel::MapView: return "map_view";
            case LogLevel::Compass: return "compass";
            default: return "?";
            }
        }

        const char* MapName(MapContext a_map)
        {
            return a_map == MapContext::World ? "world map" : "local map";
        }

        bool ShowDecision(const MarkerInfo& a_info)
        {
            return !g_config.enabled || ShouldShowMarker(a_info, g_config.markers);
        }

        struct DataLock
        {
            explicit DataLock(RE::PipboyDataGroup& a_group) :
                group(a_group) { group.LockDataGroup(); }
            ~DataLock() { group.UnlockDataGroup(); }
            DataLock(const DataLock&) = delete;
            DataLock& operator=(const DataLock&) = delete;
            RE::PipboyDataGroup& group;
        };

        // ---- Pip-Boy data tree ----------------------------------------------

        RE::PipboyValue* FindMember(RE::PipboyObject* a_obj, const char* a_name)
        {
            if (!a_obj) return nullptr;
            const auto it = a_obj->memberMap.find(RE::BSFixedString{ a_name });
            return it != a_obj->memberMap.end() ? it->second : nullptr;
        }

        RE::PipboyPrimitiveValue<bool>* FindBool(RE::PipboyObject* a_obj, const char* a_name)
        {
            auto* v = FindMember(a_obj, a_name);
            if (!v || v->GetType() != RE::PipboyValue::SERIALIZATION_DATA_TYPE::kBool) return nullptr;
            return static_cast<RE::PipboyPrimitiveValue<bool>*>(v);
        }

        RE::PipboyPrimitiveValue<std::uint32_t>* FindUInt(RE::PipboyObject* a_obj, const char* a_name)
        {
            auto* v = FindMember(a_obj, a_name);
            if (!v || v->GetType() != RE::PipboyValue::SERIALIZATION_DATA_TYPE::kUint32) return nullptr;
            return static_cast<RE::PipboyPrimitiveValue<std::uint32_t>*>(v);
        }

        // Flags the value and its parents so the change is sent to the UI.
        void MarkDirty(RE::PipboyValue* a_value)
        {
            for (int depth = 0; a_value && depth < 16; ++depth, a_value = a_value->m_parent) {
                a_value->m_isDirtyGame = true;
                a_value->m_isDirtyToCompanion = true;
            }
        }

        struct TreeTally
        {
            int shown = 0, hidden = 0, changed = 0, noMember = 0;
        };

        void ApplyVisible(RE::PipboyObject* a_obj, bool a_show, std::unordered_set<RE::PipboyObject*>& a_touched, TreeTally& a_tally)
        {
            if (!a_obj) return;
            a_touched.insert(a_obj);

            auto* vis = FindBool(a_obj, "Visible");
            if (!vis) {
                ++a_tally.noMember;
                return;
            }

            const auto saved = g_saved.find(a_obj);
            const bool ours = saved != g_saved.end() && saved->second.id == vis->m_id;
            if (saved != g_saved.end() && !ours) g_saved.erase(saved);

            if (a_show) {
                if (ours) {
                    if (vis->m_value != saved->second.original) {
                        vis->m_value = saved->second.original;
                        MarkDirty(vis);
                        ++a_tally.changed;
                    }
                    g_saved.erase(a_obj);
                }
                ++a_tally.shown;
            } else {
                if (vis->m_value) {
                    g_saved[a_obj] = SavedVisible{ vis->m_id, true };
                    vis->m_value = false;
                    MarkDirty(vis);
                    ++a_tally.changed;
                }
                ++a_tally.hidden;
            }
        }

        // Quest groups of a tree quest marker, from its `QuestId` array of quest form IDs.
        std::uint8_t QuestGroupsOf(RE::PipboyObject* a_obj)
        {
            auto* ids = FindMember(a_obj, "QuestId");
            if (!ids || ids->GetType() != RE::PipboyValue::SERIALIZATION_DATA_TYPE::kArray) return 0;

            std::uint8_t groups = 0;
            for (auto* e : static_cast<RE::PipboyArray*>(ids)->elements) {
                if (!e || e->GetType() != RE::PipboyValue::SERIALIZATION_DATA_TYPE::kUint32) continue;
                const auto  formId = static_cast<RE::PipboyPrimitiveValue<std::uint32_t>*>(e)->m_value;
                const auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(formId);
                if (!quest) continue;

                const auto type = quest->data.questType;
                const auto group = QuestGroupForType(type);
                groups |= group;
                if (LogOn(LogCategory::Quests) && !g_loggedQuestTypes) {
                    Log::Info("quest", "quest_marker of '{}' ({:08X}): quest_type={} -> {}", quest->GetFullName() ? quest->GetFullName() : "?",
                        formId, static_cast<int>(type), group == kQuestGroupMisc ? "misc" : "quests");
                }
            }
            return groups;
        }

        void CollectQuestGroups(RE::PipboyMapData& a_map)
        {
            g_questGroups.clear();
            for (auto* markers : { &a_map.worldQuestMarkers, &a_map.localQuestMarkers }) {
                for (auto& entry : *markers) {
                    if (!entry.second) continue;
                    if (const auto groups = QuestGroupsOf(entry.second)) g_questGroups[entry.second->m_id] = groups;
                }
            }
            if (!g_questGroups.empty() && LogOn(LogCategory::Quests)) g_loggedQuestTypes = true;
        }

        std::string TreeSummary(const TreeTally& a_locations, const TreeTally& a_singles)
        {
            auto s = std::format("locations shown={} hidden={}; custom_waypoint/power_armor shown={} hidden={}", a_locations.shown,
                a_locations.hidden, a_singles.shown, a_singles.hidden);
            if (const auto missing = a_locations.noMember + a_singles.noMember) s += std::format("; missing data={}", missing);
            return s;
        }

        void ApplyTree(RE::PipboyMapData& a_map, bool a_logSummary)
        {
            const DataLock                          lock{ a_map };
            std::unordered_set<RE::PipboyObject*>   touched;
            std::unordered_map<std::uint32_t, bool> locationShow;
            TreeTally                               locations, singles;

            for (auto& entry : a_map.travelLocationsMarkers) {
                RE::PipboyObject* obj = entry.second;
                if (!obj) continue;

                const auto* typeV = FindUInt(obj, "type");
                const auto* discoveredV = FindBool(obj, "Discovered");
                if (!typeV || !discoveredV) {
                    ++locations.noMember;
                    continue;
                }

                const auto type = typeV->m_value;
                const bool discovered = discoveredV->m_value;
                auto       info = ClassifyMapMarker(MapContext::World, type, discovered);

                const RE::BGSLocation* location = nullptr;
                if (const auto* locationId = FindUInt(obj, "LocationFormId")) {
                    location = RE::TESForm::GetFormByID<RE::BGSLocation>(locationId->m_value);
                }

                // Settlements with their own icon (Sanctuary, The Castle, ...) are recognised by
                // the workshop keyword of their location.
                if (info.kind == MarkerKind::Location && location && location->HasKeywordString("LocTypeWorkshopSettlement")) {
                    info.kind = MarkerKind::Settlement;
                }

                const bool show = ShowDecision(info);
                if (LogOn(LogCategory::Locations) && !g_loggedLocations) {
                    const char* name = location && location->GetFullName() ? location->GetFullName() : "?";
                    Log::Info("location", "'{}' {} ({}, icon={}) -> {}", name, KindName(info.kind),
                        discovered ? "discovered" : "undiscovered", type, show ? "shown" : "hidden");
                }
                ApplyVisible(obj, show, touched, locations);
                locationShow[obj->m_id] = show;
            }
            if (LogOn(LogCategory::Locations)) g_loggedLocations = true;

            const auto single = [&](RE::PipboyObject* a_obj, MapContext a_context, MarkerKind a_kind) {
                ApplyVisible(a_obj, ShowDecision(MarkerInfo{ a_context, a_kind }), touched, singles);
            };
            single(a_map.worldCustomMarker, MapContext::World, MarkerKind::CustomWaypoint);
            single(a_map.localCustomMarker, MapContext::Local, MarkerKind::CustomWaypoint);
            single(a_map.worldPowerArmorMarker, MapContext::World, MarkerKind::PowerArmor);
            single(a_map.localPowerArmorMarker, MapContext::Local, MarkerKind::PowerArmor);

            CollectQuestGroups(a_map);
            g_locationShow = std::move(locationShow);

            // Forget objects that no longer exist in the tree.
            for (auto it = g_saved.begin(); it != g_saved.end();) {
                it = touched.contains(it->first) ? std::next(it) : g_saved.erase(it);
            }

            if (a_logSummary && LogOn(LogCategory::Locations)) {
                auto summary = TreeSummary(locations, singles);
                if (summary != g_lastTreeSummary) Log::Info("location", "map update: {}", summary);
                g_lastTreeSummary = std::move(summary);
            }
        }

        // ---- Scaleform values -------------------------------------------------

        bool ToUInt(const GValue& a_v, std::uint32_t& a_out)
        {
            if (a_v.IsUInt()) {
                a_out = a_v.GetUInt();
            } else if (a_v.IsInt()) {
                a_out = static_cast<std::uint32_t>(a_v.GetInt());
            } else if (a_v.IsNumber()) {
                a_out = static_cast<std::uint32_t>(a_v.GetNumber());
            } else {
                return false;
            }
            return true;
        }

        bool ToNumber(const GValue& a_v, double& a_out)
        {
            if (a_v.IsNumber()) {
                a_out = a_v.GetNumber();
            } else if (a_v.IsUInt()) {
                a_out = a_v.GetUInt();
            } else if (a_v.IsInt()) {
                a_out = a_v.GetInt();
            } else {
                return false;
            }
            return true;
        }

        bool GetUIntMember(const GValue& a_obj, const char* a_name, std::uint32_t& a_out)
        {
            GValue v;
            return a_obj.GetMember(a_name, &v) && ToUInt(v, a_out);
        }

        bool GetNumberMember(const GValue& a_obj, const char* a_name, double& a_out)
        {
            GValue v;
            return a_obj.GetMember(a_name, &v) && ToNumber(v, a_out);
        }

        bool GetBoolMember(const GValue& a_obj, const char* a_name)
        {
            GValue v;
            return a_obj.GetMember(a_name, &v) && v.IsBoolean() && v.GetBoolean();
        }

        // AS3 class name of a display object, from clip.constructor.toString() ("[class QuestMarker]").
        bool ClipClassName(const GValue& a_clip, std::string& a_out)
        {
            GValue ctor;
            if (!a_clip.GetMember("constructor", &ctor) || !ctor.IsObject()) return false;
            GValue str;
            if (!ctor.Invoke("toString", &str) || !str.IsString() || !str.GetString()) return false;
            a_out = FlashClassName(str.GetString());
            return true;
        }

        // One example SWF marker entry with all its members, e.g. name='...' type=82 x=-22306.6.
        void LogFlashEntry(MarkerKind a_kind, const GValue& a_entry)
        {
            std::string members;
            a_entry.VisitMembers([&](const char* a_name, const GValue& a_member) {
                double number = 0.0;
                members += std::format(" {}=", a_name ? a_name : "?");
                if (a_member.IsBoolean()) {
                    members += a_member.GetBoolean() ? "true" : "false";
                } else if (a_member.IsString() && a_member.GetString()) {
                    members += std::format("'{}'", a_member.GetString());
                } else if (ToNumber(a_member, number)) {
                    members += std::format("{:.6g}", number);
                } else {
                    members += "?";
                }
            });
            Log::Info("quest", "example {} entry:{}", KindName(a_kind), members);
        }

        // ---- SWF marker arrays --------------------------------------------------

        struct FlashTally
        {
            int entries = 0, unknown = 0, shown = 0, removed = 0, failed = 0, questMatched = 0, questUnmatched = 0, locationsRemoved = 0;
        };

        // Clip class for a SWF marker type. The class of a real clip wins (a replaced SWF may
        // differ from vanilla); until one has been seen, the vanilla table is used.
        std::string_view FlashClassFor(std::uint32_t a_type, const GValue& a_entry)
        {
            if (const auto it = g_flashClassByType.find(a_type); it != g_flashClassByType.end()) return it->second;

            GValue      clip;
            std::string name;
            if (!a_entry.GetMember("clip", &clip) || !clip.IsDisplayObject() || !ClipClassName(clip, name)) {
                return VanillaFlashMarkerClass(a_type);
            }
            const auto vanilla = VanillaFlashMarkerClass(a_type);
            if (!vanilla.empty() && vanilla != name) {
                Log::Warn("map marker type {} uses clip {} instead of vanilla {}, following the clip", a_type, name, vanilla);
            } else if (LogOn(LogCategory::MarkerHiding)) {
                Log::Info("map", "marker type {} -> {} (clip {})", a_type, KindName(ClassifyFlashMarkerClass(MapContext::World, name).kind), name);
            }
            return g_flashClassByType.emplace(a_type, std::move(name)).first->second;
        }

        // Removes one entry. If its clip already exists, the clip is hidden too, because the SWF
        // stops updating clips that are no longer in the array.
        bool RemoveFlashEntry(GValue& a_array, std::uint32_t a_index, const GValue& a_entry)
        {
            GValue clip;
            if (a_entry.GetMember("clip", &clip) && clip.IsDisplayObject()) {
                clip.SetMember("visible", GValue(false));
                clip.SetMember("alpha", GValue(0.0));
            }
            return a_array.RemoveElement(a_index);
        }

        // Removes hidden quest, player and location entries from one SWF marker array. The SWF
        // matches selection, centering and fast travel by `arrayIndex`, not by position, so
        // removing entries is safe.
        void FilterFlashArray(GValue& a_dataObj, const char* a_arrayName, MapContext a_map, FlashTally& a_tally)
        {
            GValue array;
            if (!a_dataObj.GetMember(a_arrayName, &array) || !array.IsArray()) {
                if (!g_loggedFlashMissing) {
                    g_loggedFlashMissing = true;
                    Log::Warn("{} marker list ({}) not found, quest and player markers stay visible", MapName(a_map), a_arrayName);
                }
                return;
            }

            const auto size = std::min(array.GetArraySize(), kMaxFlashMarkers);
            for (std::uint32_t i = size; i-- > 0;) {  // backwards, so removal keeps indices valid
                GValue entry;
                if (!array.GetElement(i, &entry) || !entry.IsObject()) continue;
                ++a_tally.entries;

                std::uint32_t type = 0;
                if (!GetUIntMember(entry, "type", type)) {
                    ++a_tally.unknown;
                    continue;
                }
                const auto    className = FlashClassFor(type, entry);
                auto          info = ClassifyFlashMarkerClass(a_map, className);
                std::uint32_t id = 0;
                const bool    haveId = GetUIntMember(entry, "arrayIndex", id);

                if (info.kind == MarkerKind::QuestObjective) {
                    const auto it = haveId ? g_questGroups.find(id) : g_questGroups.end();
                    if (it != g_questGroups.end()) {
                        info.questGroups = it->second;
                        ++a_tally.questMatched;
                    } else {
                        ++a_tally.questUnmatched;  // group unknown: hidden only if both groups are hidden
                    }
                }

                if (info.kind == MarkerKind::Unknown) {
                    // Locations: the game itself sets some quest-related locations visible again
                    // during UpdateData, so the tree decision is applied here as well.
                    const auto location = haveId ? g_locationShow.find(id) : g_locationShow.end();
                    if (location == g_locationShow.end() || location->second) {
                        ++a_tally.unknown;
                        continue;
                    }
                    if (LogOn(LogCategory::Locations) && g_reportedLeaks.insert(id).second) {
                        GValue name;
                        entry.GetMember("name", &name);
                        Log::Info("location", "'{}' re-shown by the game on the {}, hidden again",
                            name.IsString() && name.GetString() ? name.GetString() : "?", MapName(a_map));
                    }
                    if (RemoveFlashEntry(array, i, entry)) {
                        ++a_tally.locationsRemoved;
                    } else {
                        ++a_tally.failed;
                    }
                    continue;
                }

                if (LogOn(LogCategory::Quests) && !g_dumpedFlashEntry && info.kind != MarkerKind::Player) {
                    g_dumpedFlashEntry = true;
                    LogFlashEntry(info.kind, entry);
                }

                if (ShowDecision(info)) {
                    ++a_tally.shown;
                } else if (RemoveFlashEntry(array, i, entry)) {
                    ++a_tally.removed;
                } else {
                    ++a_tally.failed;
                }
            }
        }

        // "markers=40 hidden=3 shown=4 not_handled=33", plus problems when there are any.
        std::string FlashSummary(const FlashTally& a_t)
        {
            auto s = std::format("markers={} hidden={} shown={} not_handled={}", a_t.entries, a_t.removed + a_t.locationsRemoved, a_t.shown, a_t.unknown);
            if (a_t.failed) s += std::format(" failed_to_hide={}", a_t.failed);
            if (a_t.questUnmatched) s += std::format(" quest_group_unknown={}", a_t.questUnmatched);
            return s;
        }

        void ApplyFlash(RE::PipboyMapMenu* a_menu, bool a_logSummary)
        {
            if (!a_menu || !a_menu->dataObj || !a_menu->dataObj->IsObject()) return;

            FlashTally world, local;
            FilterFlashArray(*a_menu->dataObj, "WorldMapMarkers", MapContext::World, world);
            FilterFlashArray(*a_menu->dataObj, "LocalMapMarkers", MapContext::Local, local);

            if (!a_logSummary || (!LogOn(LogCategory::MarkerHiding) && !LogOn(LogCategory::Quests) && !LogOn(LogCategory::Locations))) return;
            auto summary = std::format("world map {}; local map {}", FlashSummary(world), FlashSummary(local));
            if (summary != g_lastFlashSummary) Log::Info("map", "{}", summary);
            g_lastFlashSummary = std::move(summary);
        }

        // ---- world map view with a hidden player ---------------------------------

        bool MoveMapBy(GValue& a_holder, double a_dx, double a_dy)
        {
            const std::array<GValue, 2> delta{ GValue(a_dx), GValue(a_dy) };
            return a_holder.Invoke("MoveMap", delta);
        }

        // True if the clip and all its ancestors are visible.
        bool ShownInDisplayList(const GValue& a_clip)
        {
            if (!a_clip.IsDisplayObject()) return false;
            GValue current = a_clip;
            for (int depth = 0; depth < 32; ++depth) {
                if (!GetBoolMember(current, "visible")) return false;
                GValue parent;
                if (!current.GetMember("parent", &parent) || !parent.IsDisplayObject()) return true;
                current = parent;
            }
            return true;
        }

        // Current page of the Pip-Boy (RE::PIPBOY_PAGES). The PipboyMapMenu is a member of
        // PipboyMenu at +0x1C0 (RE/P/PipboyMenu.h); the containing object is used only if its
        // vtable is PipboyMenu's.
        std::optional<std::uint32_t> RootCurrentPage(RE::PipboyMapMenu* a_mapMenu)
        {
            constexpr std::uintptr_t kMapMenuOffset = 0x1C0;
            static const auto        expected = REL::Relocation<std::uintptr_t>{ RE::VTABLE::PipboyMenu[0] }.address();
            auto* const              menu = reinterpret_cast<RE::PipboyMenu*>(reinterpret_cast<std::uintptr_t>(a_mapMenu) - kMapMenuOffset);
            if (*reinterpret_cast<const std::uintptr_t*>(menu) != expected || !menu->dataObj.IsObject()) return std::nullopt;
            std::uint32_t page = 0;
            if (!GetUIntMember(menu->dataObj, "CurrentPage", page)) return std::nullopt;
            return page;
        }

        // With the player marker hidden the SWF has nothing to center on and stays at the top-left
        // corner, so the map is zoomed out and centered instead. Only public SWF members are
        // usable: START_ZOOM, bMapLoaded and the pan limits are `protected`. MoveMap clamps to the
        // pan range, so moving far past each edge finds the range, then the map moves to its middle.
        //
        // Applied once per Pip-Boy open and whenever the map becomes visible again, because the
        // SWF then resets the zoom. If that reset lands just after we applied (within 1 s), apply
        // once more.
        void ApplyHiddenPlayerView(RE::PipboyMapMenu* a_menu)
        {
            using Clock = std::chrono::steady_clock;
            auto& state = g_worldView;

            // On another Pip-Boy page the map is off stage and the holder cannot be fetched. That
            // must count as hidden, otherwise returning to the map is not noticed.
            GValue     holder;
            const bool haveHolder = a_menu->mapPageObj.IsObject() && a_menu->mapPageObj.GetMember("WorldMapHolder_mc", &holder) && holder.IsDisplayObject();
            const bool inDisplayList = haveHolder && ShownInDisplayList(a_menu->mapPageObj);
            const auto rootPage = RootCurrentPage(a_menu);
            const bool pageShown = inDisplayList && (!rootPage || *rootPage == static_cast<std::uint32_t>(RE::PIPBOY_PAGES::kMap));
            const bool visible = pageShown && GetBoolMember(holder, "visible");

            if (pageShown != state.wasPageShown && LogOn(LogCategory::MapView)) {
                Log::Info("map_view", "map page {}", pageShown ? "opened" : "closed");
            }
            state.wasPageShown = pageShown;
            const bool becameVisible = visible && !state.wasVisible;
            state.wasVisible = visible;
            if (!haveHolder) return;

            if (ShowDecision(MarkerInfo{ MapContext::World, MarkerKind::Player })) {
                state.centerPending = false;
                state.appliedAt.reset();
                return;
            }

            const char* reason = state.centerPending ? "Pip-Boy opened" : nullptr;
            if (becameVisible) {
                state.centerPending = true;
                reason = "map became visible";
            }
            if (state.appliedAt) {
                double scale = 0.0;
                if (Clock::now() - *state.appliedAt > std::chrono::seconds(1)) {
                    state.appliedAt.reset();
                } else if (GetNumberMember(holder, "mapScale", scale) && scale > 0.001) {
                    state.centerPending = true;
                    state.appliedAt.reset();
                    reason = "game reset the zoom";
                }
            }
            if (!state.centerPending || !visible || !GetBoolMember(holder, "mapLoaded")) return;  // retry next frame

            state.centerPending = false;
            state.appliedAt = Clock::now();
            holder.SetMember("mapScale", GValue(0.0));  // fully zoomed out; also recomputes the pan range

            constexpr double kFar = 1.0e7;
            double           maxX = 0.0, maxY = 0.0, minX = 0.0, minY = 0.0;
            const bool       ok = MoveMapBy(holder, kFar, kFar) && GetNumberMember(holder, "x", maxX) && GetNumberMember(holder, "y", maxY) &&
                MoveMapBy(holder, -2.0 * kFar, -2.0 * kFar) && GetNumberMember(holder, "x", minX) && GetNumberMember(holder, "y", minY) &&
                MoveMapBy(holder, (maxX - minX) / 2.0, (maxY - minY) / 2.0);
            if (!ok) {
                Log::Warn("player marker hidden, but the world map could not be centered");
            } else if (LogOn(LogCategory::MapView)) {
                Log::Info("map_view", "player marker hidden: world map zoomed out and centered ({})", reason ? reason : "?");
            }
        }

        // ---- hook and events -----------------------------------------------------

        // Checks once that PipboyDataManager::mapData is where this build expects it.
        bool VerifyLayout(RE::PipboyDataManager& a_manager)
        {
            if (g_layoutChecked) return g_layoutVerified;
            g_layoutChecked = true;

            const auto vptr = *reinterpret_cast<const std::uintptr_t*>(&a_manager.mapData);
            const auto expected = REL::Relocation<std::uintptr_t>{ RE::VTABLE::PipboyMapData[0] }.address();
            g_layoutVerified = vptr == expected;
            if (g_layoutVerified) {
                Log::Info("plugin", "Pip-Boy map data check passed");
            } else {
                Log::Error("Pip-Boy map data does not match this game version, map filter disabled (vtable {:#x}, expected {:#x})", vptr, expected);
            }
            return g_layoutVerified;
        }

        void ApplyFilter(RE::PipboyMapMenu* a_menu, bool a_logSummary)
        {
            try {
                auto* manager = RE::PipboyDataManager::GetSingleton();
                if (!manager || !VerifyLayout(*manager)) return;
                ApplyTree(manager->mapData, a_logSummary);  // under the data-group lock
                ApplyFlash(a_menu, a_logSummary);
            } catch (...) {
                Log::Error("Pip-Boy map filter failed, markers left as they are");
            }
        }

        void Hook_UpdateData(RE::PipboyMapMenu* a_this)
        {
            // Summaries are logged after the original only, where the result is final.
            g_mapMenu = a_this;
            ApplyFilter(a_this, false);
            reinterpret_cast<void (*)(RE::PipboyMapMenu*)>(g_originalUpdateData)(a_this);
            ApplyFilter(a_this, true);
        }

        void ApplyViewForFrame()
        {
            auto* mapMenu = g_mapMenu.load();
            if (!mapMenu || g_viewDisabled) return;

            static const auto expected = REL::Relocation<std::uintptr_t>{ RE::VTABLE::PipboyMapMenu[0] }.address();
            const auto        vptr = *reinterpret_cast<const std::uintptr_t*>(mapMenu);
            if (vptr != expected) {
                Log::Error("Pip-Boy map menu does not match this game version, map zoom/center disabled (vtable {:#x}, expected {:#x})", vptr, expected);
                g_viewDisabled = true;
                return;
            }
            if (g_centerRequested.exchange(false)) g_worldView.centerPending = true;
            ApplyHiddenPlayerView(mapMenu);
        }

        // Sent every frame after the UI advanced all menus. Used for the world map view (so it
        // does not wait for the next UpdateData, which may only come on hover) and the HUD.
        // Note: re-queueing an F4SE UI task from inside itself instead froze the game.
        class FrameSink final : public RE::BSTEventSink<RE::UIAdvanceMenusFunctionCompleteEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::UIAdvanceMenusFunctionCompleteEvent&, RE::BSTEventSource<RE::UIAdvanceMenusFunctionCompleteEvent>*) override
            {
                try {
                    ApplyViewForFrame();
                } catch (...) {
                    Log::Error("map zoom/center failed and is disabled until restart");
                    g_viewDisabled = true;
                }
                if (!g_compassDisabled) {
                    try {
                        Compass::OnFrame(g_config);
                    } catch (...) {
                        Log::Error("compass marker hiding failed and is disabled until restart");
                        g_compassDisabled = true;
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        FrameSink g_frameSink;

        // Reloads settings when the Pip-Boy opens or the pause menu (where MCM lives) closes.
        class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                const std::string_view name{ a_event.menuName.data(), a_event.menuName.size() };
                if (name == RE::PauseMenu::MENU_NAME && !a_event.opening) {
                    ReloadSettings();
                } else if (name == RE::PipboyMenu::MENU_NAME) {
                    if (a_event.opening) {
                        ReloadSettings();
                        g_centerRequested = true;
                        g_reportedLeaks.clear();
                    } else {
                        g_mapMenu = nullptr;
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        MenuSink g_menuSink;
    }

    // Settings are reloaded often (every Pip-Boy open), so they are logged only when they change.
    void ReloadSettings()
    {
        static bool loaded = false;
        auto        config = LoadConfig(kDefaultIniPath, kUserIniPath);
        if (loaded && config == g_config) return;
        loaded = true;
        g_config = std::move(config);

        using Log::OnOff;
        const auto& m = g_config.markers;
        const auto& c = g_config.compass;
        Log::Info("settings", "mod={} logging={}", OnOff(g_config.enabled), LogLevelName(g_config.logLevel));
        Log::Info("settings", "map: undiscovered={} discovered={} settlements={} quests={} misc={} quest_doors={} player={} custom_waypoint={} power_armor={}",
            OnOff(m.showUndiscoveredLocations), OnOff(m.showDiscoveredLocations), OnOff(m.showDiscoveredSettlements),
            VisibilityName(m.questObjectives), VisibilityName(m.miscObjectives), VisibilityName(m.questDoors),
            VisibilityName(m.player), VisibilityName(m.customWaypoint), VisibilityName(m.powerArmor));
        Log::Info("settings", "hud: compass={} quests={} enemies={} undiscovered={} discovered={} custom_waypoint={} directions={} floating_quests={}",
            OnOff(c.showCompass), OnOff(c.showQuests), OnOff(c.showEnemies), OnOff(c.showUndiscoveredLocations),
            OnOff(c.showDiscoveredLocations), OnOff(c.showCustomWaypoint), OnOff(c.showDirections), OnOff(c.showFloatingQuests));
    }

    bool Install()
    {
        if (g_installed) return true;

        ReloadSettings();

        // Hook only if the vtable slot holds exactly the UpdateData this build was written for.
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::PipboyMapMenu[0] };
        const auto                      slot = vtable.address() + sizeof(std::uintptr_t) * kUpdateDataVtableSlot;
        const auto                      current = *reinterpret_cast<const std::uintptr_t*>(slot);
        const auto                      expected = REL::ID(RE::ID::PipboyMapMenu::UpdateData).address();
        if (current != expected) {
            Log::Error("Pip-Boy map hook not installed: unexpected game code, wrong game version or another mod hooks it ({:#x}, expected {:#x})",
                current, expected);
            return false;
        }

        if (auto* ui = RE::UI::GetSingleton()) {
            ui->RegisterSink<RE::MenuOpenCloseEvent>(&g_menuSink);
        } else {
            Log::Warn("UI not available, settings load only once at startup");
        }

        if (auto* frames = RE::UIAdvanceMenusFunctionCompleteEvent::GetEventSource()) {
            frames->RegisterSink(&g_frameSink);
        } else {
            Log::Warn("UI frame event not available, no map zoom/center and no compass marker hiding");
        }

        g_originalUpdateData = vtable.write_vfunc(kUpdateDataVtableSlot, &Hook_UpdateData);
        g_installed = true;
        Log::Info("plugin", "installed");
        return true;
    }
}
