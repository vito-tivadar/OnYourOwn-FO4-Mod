#include "HudCompass.h"

#include "CompassPolicy.h"
#include "Log.h"
#include "MarkerClassifier.h"

#include <RE/Fallout.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// HUD marker hiding (HUDMenu.swf; vanilla and HUDFramework are identical here).
//
//  * Compass: root.BottomCenterGroup_mc.CompassWidget_mc holds QuestMarkerHolder_mc and
//    OtherMarkerHolder_mc. The game drives the marker clips directly: it sets each marker's frame
//    (the category label), its `visible` flag and the inner icon's alpha, but never the marker's
//    own alpha. Hidden markers get that alpha set to 0 every frame.
//  * Floating markers: FloatingTarget clips under root.FloatingQuestMarkerBase. The game fades
//    their own alpha every frame, so the icon inside (FloatingTargetSubClip) is hidden instead.
//
// Only clips we hid are ever restored, and with every option on nothing is touched, so iHUD and
// similar mods keep full control.

namespace OnYourOwn::Compass
{
    namespace
    {
        using GValue = ::Scaleform::GFx::Value;

        constexpr int                        kMaxChildren = 256;
        constexpr std::array<const char*, 2> kHolders{ "QuestMarkerHolder_mc", "OtherMarkerHolder_mc" };

        // ---- Scaleform helpers ------------------------------------------------

        double Number(const GValue& a_obj, const char* a_name, double a_default = -1.0)
        {
            GValue v;
            if (!a_obj.GetMember(a_name, &v)) return a_default;
            if (v.IsNumber()) return v.GetNumber();
            if (v.IsUInt()) return v.GetUInt();
            if (v.IsInt()) return v.GetInt();
            return a_default;
        }

        std::string String(const GValue& a_obj, const char* a_name)
        {
            GValue v;
            return a_obj.GetMember(a_name, &v) && v.IsString() && v.GetString() ? v.GetString() : std::string{};
        }

        std::string ClassOf(const GValue& a_clip)
        {
            GValue ctor, str;
            if (!a_clip.GetMember("constructor", &ctor) || !ctor.IsObject()) return "?";
            if (!ctor.Invoke("toString", &str) || !str.IsString() || !str.GetString()) return "?";
            return std::string{ FlashClassName(str.GetString()) };
        }

        bool Child(const GValue& a_parent, const char* a_name, GValue& a_out)
        {
            return (a_parent.IsObject() || a_parent.IsDisplayObject()) && a_parent.GetMember(a_name, &a_out) && a_out.IsDisplayObject();
        }

        std::vector<GValue> Children(GValue a_clip)  // by value: GFx::Value::Invoke is not const
        {
            std::vector<GValue> out;
            const auto          count = static_cast<int>(Number(a_clip, "numChildren", 0.0));
            for (int i = 0; i < count && i < kMaxChildren; ++i) {
                const std::array<GValue, 1> args{ GValue(i) };
                GValue                      child;
                if (a_clip.Invoke("getChildAt", &child, args) && child.IsDisplayObject()) out.push_back(child);
            }
            return out;
        }

        void SetAlpha(GValue& a_clip, double a_alpha)
        {
            a_clip.SetMember("alpha", GValue(a_alpha));
        }

        // Discovered state of a location marker, from its icon clip's frame label.
        std::optional<bool> LocationDiscovered(const GValue& a_marker)
        {
            for (const auto& icon : Children(a_marker)) {
                if (const auto d = CompassLocationDiscovered(String(icon, "currentLabel"))) return d;
            }
            return std::nullopt;
        }

        // The FloatingTarget for one child of FloatingQuestMarkerBase: the child itself or one
        // of its own children.
        bool FloatingTargetOf(const GValue& a_clip, GValue& a_target)
        {
            if (ClassOf(a_clip) == "FloatingTarget") {
                a_target = a_clip;
                return true;
            }
            for (const auto& child : Children(a_clip)) {
                if (ClassOf(child) == "FloatingTarget") {
                    a_target = child;
                    return true;
                }
            }
            return false;
        }

        bool FindRoot(GValue& a_root, std::string& a_error)
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                a_error = "UI not available";
                return false;
            }
            const auto menu = ui->GetMenu(RE::HUDMenu::MENU_NAME);
            if (!menu) {
                a_error = "HUDMenu not open";
                return false;
            }
            a_root = menu->menuObj;
            if (!a_root.IsObject() && !a_root.IsDisplayObject() && menu->uiMovie) {
                menu->uiMovie->GetVariable(&a_root, "root");
            }
            if (!a_root.IsObject() && !a_root.IsDisplayObject()) {
                a_error = "HUD menu has no root clip";
                return false;
            }
            return true;
        }

        bool FindWidget(const GValue& a_root, GValue& a_widget, std::string& a_error)
        {
            GValue group;
            if (!Child(a_root, "BottomCenterGroup_mc", group)) {
                a_error = "HUD has no BottomCenterGroup_mc (replaced HUD?)";
                return false;
            }
            if (!Child(group, "CompassWidget_mc", a_widget)) {
                a_error = "HUD has no CompassWidget_mc (replaced HUD?)";
                return false;
            }
            return true;
        }

        // ---- hiding -------------------------------------------------------------

        // Counts for the log. `reset` = clips we hid whose alpha the game set back since the last
        // frame; it stays 0 when hiding sticks.
        struct Tally
        {
            int quests = 0, enemies = 0, undiscovered = 0, discovered = 0, waypoint = 0, directions = 0, reset = 0;
            int floatingHidden = 0, floatingReset = 0;
        };

        struct State
        {
            std::string       lastError;
            bool              widgetHidden = false;
            std::vector<bool> hidden[kHolders.size()];  // compass markers we hid, by holder and child index
            std::vector<bool> floatingHidden;           // floating markers we hid, by child index
            std::string       lastSummary;
        };
        State g_state;

        bool AnyHidden()
        {
            const auto any = [](const std::vector<bool>& a_flags) {
                for (const bool b : a_flags) {
                    if (b) return true;
                }
                return false;
            };
            return g_state.widgetHidden || any(g_state.floatingHidden) || any(g_state.hidden[0]) || any(g_state.hidden[1]);
        }

        // Hides or restores one clip. a_wasHidden tracks whether we hid it.
        void Apply(GValue& a_clip, bool a_show, bool& a_wasHidden, int& a_resetCount)
        {
            if (!a_show) {
                if (Number(a_clip, "alpha", 0.0) != 0.0) {
                    if (a_wasHidden) ++a_resetCount;
                    SetAlpha(a_clip, 0.0);
                }
                a_wasHidden = true;
            } else if (a_wasHidden) {
                SetAlpha(a_clip, 1.0);
                a_wasHidden = false;
            }
        }

        void CountHidden(Tally& a_tally, CompassMarkerKind a_kind, std::optional<bool> a_discovered)
        {
            switch (a_kind) {
            case CompassMarkerKind::Quest:
            case CompassMarkerKind::QuestDoor:
                ++a_tally.quests;
                break;
            case CompassMarkerKind::Enemy:
                ++a_tally.enemies;
                break;
            case CompassMarkerKind::Location:
                ++(a_discovered.value_or(true) ? a_tally.discovered : a_tally.undiscovered);
                break;
            case CompassMarkerKind::CustomWaypoint:
                ++a_tally.waypoint;
                break;
            case CompassMarkerKind::Direction:
                ++a_tally.directions;
                break;
            default:
                break;
            }
        }

        void ApplyCompassMarkers(GValue& a_widget, const CompassSettings& a_settings, bool a_active, Tally& a_tally)
        {
            for (std::size_t h = 0; h < kHolders.size(); ++h) {
                auto&  hidden = g_state.hidden[h];
                GValue holder;
                if (!Child(a_widget, kHolders[h], holder)) {
                    hidden.clear();
                    continue;
                }
                auto markers = Children(holder);
                hidden.resize(markers.size(), false);
                for (std::size_t i = 0; i < markers.size(); ++i) {
                    bool show = true;
                    if (a_active) {
                        const auto kind = ClassifyCompassMarker(ClassOf(markers[i]), String(markers[i], "currentLabel"));
                        const auto discovered = kind == CompassMarkerKind::Location ? LocationDiscovered(markers[i]) : std::nullopt;
                        show = ShouldShowCompassMarker(kind, discovered, a_settings);
                        if (!show) CountHidden(a_tally, kind, discovered);
                    }
                    bool wasHidden = hidden[i];
                    Apply(markers[i], show, wasHidden, a_tally.reset);
                    hidden[i] = wasHidden;
                }
            }
        }

        void ApplyFloatingMarkers(const GValue& a_root, const CompassSettings& a_settings, bool a_active, Tally& a_tally)
        {
            auto&  hidden = g_state.floatingHidden;
            GValue base;
            if (!Child(a_root, "FloatingQuestMarkerBase", base)) {
                hidden.clear();
                return;
            }
            auto clips = Children(base);
            hidden.resize(clips.size(), false);
            for (std::size_t i = 0; i < clips.size(); ++i) {
                GValue     target, icon;
                const bool haveTarget = FloatingTargetOf(clips[i], target);
                if (!haveTarget || !Child(target, "FloatingTargetSubClip", icon)) icon = haveTarget ? target : clips[i];

                bool show = true;
                if (a_active && haveTarget) {
                    const auto kind = ClassifyCompassMarker("FloatingTarget", String(target, "currentLabel"));
                    const auto discovered = kind == CompassMarkerKind::Location ? LocationDiscovered(target) : std::nullopt;
                    show = ShouldShowFloatingMarker(kind, discovered, a_settings);
                    if (!show) ++a_tally.floatingHidden;
                }
                bool wasHidden = hidden[i];
                Apply(icon, show, wasHidden, a_tally.floatingReset);
                hidden[i] = wasHidden;
            }
        }

        // ---- diagnostics (Compass log level) --------------------------------------

        const char* KindName(CompassMarkerKind a_kind)
        {
            switch (a_kind) {
            case CompassMarkerKind::Quest: return "quest_marker";
            case CompassMarkerKind::QuestDoor: return "quest_door";
            case CompassMarkerKind::CustomWaypoint: return "custom_waypoint";
            case CompassMarkerKind::Recon: return "recon_target";
            case CompassMarkerKind::Enemy: return "enemy";
            case CompassMarkerKind::Location: return "location";
            case CompassMarkerKind::Direction: return "direction";
            default: return "unknown";
            }
        }

        // "location (undiscovered)", or for clips we do not know "unknown (class Foo, frame 'Bar')".
        std::string MarkerName(const GValue& a_clip, std::string_view a_className)
        {
            const auto label = String(a_clip, "currentLabel");
            const auto kind = ClassifyCompassMarker(a_className, label);
            if (kind == CompassMarkerKind::Unknown) return std::format("unknown (class {}, frame '{}')", a_className, label);
            if (kind != CompassMarkerKind::Location) return KindName(kind);
            const auto discovered = LocationDiscovered(a_clip);
            return std::format("location ({})", !discovered ? "unknown state" : *discovered ? "discovered" : "undiscovered");
        }

        // "direction x4, location (discovered) x2, quest_door", or "none".
        std::string CountList(const std::map<std::string, int>& a_counts)
        {
            std::string s;
            for (const auto& [name, count] : a_counts) {
                if (!s.empty()) s += ", ";
                s += count > 1 ? std::format("{} x{}", name, count) : name;
            }
            return s.empty() ? "none" : s;
        }

        // "quest_marker=1 enemy=2", only the non-zero counts, or "nothing".
        std::string HiddenList(std::initializer_list<std::pair<const char*, int>> a_counts)
        {
            std::string s;
            for (const auto& [name, count] : a_counts) {
                if (count) s += std::format("{}{}={}", s.empty() ? "" : " ", name, count);
            }
            return s.empty() ? "nothing" : s;
        }

        // Lists which markers exist on the compass and in the world when that changes; checked
        // at most twice a second.
        void LogMarkers(const GValue& a_root, const GValue& a_widget)
        {
            static auto        last = std::chrono::steady_clock::time_point{};
            static std::string lastLines;
            const auto         now = std::chrono::steady_clock::now();
            if (now - last < std::chrono::milliseconds(500)) return;
            last = now;

            std::map<std::string, int> compass, floating;
            for (const char* holderName : kHolders) {
                GValue holder;
                if (!Child(a_widget, holderName, holder)) continue;
                for (const auto& marker : Children(holder)) ++compass[MarkerName(marker, ClassOf(marker))];
            }
            GValue base;
            if (Child(a_root, "FloatingQuestMarkerBase", base)) {
                for (const auto& clip : Children(base)) {
                    GValue target;
                    if (FloatingTargetOf(clip, target)) {
                        ++floating[MarkerName(target, "FloatingTarget")];
                    } else {
                        ++floating[std::format("unknown (class {})", ClassOf(clip))];
                    }
                }
            }

            auto lines = std::format("compass: {}\nfloating: {}", CountList(compass), CountList(floating));
            if (lines == lastLines) return;
            Log::Info("compass", "on compass: {}", CountList(compass));
            Log::Info("compass", "in world: {}", CountList(floating));
            lastLines = std::move(lines);
        }
    }

    void OnFrame(const Config& a_config)
    {
        const bool logging = ShouldLog(a_config.logLevel, LogCategory::Compass);
        const bool active = a_config.enabled && !a_config.compass.AllShown();
        if (!active && !AnyHidden() && !logging) return;

        GValue      root, widget;
        std::string error;
        if (!FindRoot(root, error) || !FindWidget(root, widget, error)) {
            if (logging && error != g_state.lastError) Log::Info("compass", "compass not found: {}", error);
            g_state.lastError = std::move(error);
            return;
        }
        g_state.lastError.clear();

        // Whole compass. With it shown we only undo our own change, so iHUD keeps control.
        if (active && !a_config.compass.showCompass) {
            if (Number(widget, "alpha", 0.0) != 0.0) SetAlpha(widget, 0.0);
            g_state.widgetHidden = true;
        } else if (g_state.widgetHidden) {
            SetAlpha(widget, 1.0);
            g_state.widgetHidden = false;
        }

        Tally tally;
        ApplyCompassMarkers(widget, a_config.compass, active && a_config.compass.showCompass, tally);
        ApplyFloatingMarkers(root, a_config.compass, active, tally);

        if (logging) {
            auto summary = std::format("compass {}, hidden: {}; in world hidden: {}", g_state.widgetHidden ? "hidden" : "shown",
                HiddenList({ { "quest_marker", tally.quests }, { "enemy", tally.enemies }, { "undiscovered_location", tally.undiscovered },
                    { "discovered_location", tally.discovered }, { "custom_waypoint", tally.waypoint }, { "direction", tally.directions } }),
                HiddenList({ { "quest_marker", tally.floatingHidden } }));
            // Should not happen: it would mean the game undoes our hiding every frame.
            if (const auto reset = tally.reset + tally.floatingReset) summary += std::format("; re-shown by the game: {}", reset);
            if (summary != g_state.lastSummary) Log::Info("compass", "{}", summary);
            g_state.lastSummary = std::move(summary);
            LogMarkers(root, widget);
        }
    }
}
