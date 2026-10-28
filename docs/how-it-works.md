# How On Your Own works

On Your Own is an F4SE plugin. It never edits SWF files or game data: everything happens at
runtime, on what the game sends to its user interface. Nothing persistent is changed (discovery,
quests, fast travel, map marker forms, saves), and anything it does not recognise is left as the
game made it.

## Source layout

| File | Purpose |
| --- | --- |
| `src/Plugin.cpp` | F4SE entry point; installs everything at `kGameDataReady`. |
| `src/PipboyFilter.cpp` | Pip-Boy map hook, settings reload, per-frame UI events. |
| `src/HudCompass.cpp` | Compass and floating in-world marker hiding. |
| `src/MarkerPolicy.*`, `src/CompassPolicy.*` | Pure "show or hide?" decisions. |
| `src/MarkerClassifier.*` | Game marker types and SWF class names to marker kinds. |
| `src/IniSettings.*` | Reads the MCM settings files. |
| `tests/` | Host tests for everything that does not need the game. |
| `Data/MCM/Config/OnYourOwn/` | MCM menu (`config.json`) and defaults (`settings.ini`). |

## Settings

Settings are layered: built-in defaults, then `Data/MCM/Config/OnYourOwn/settings.ini`, then the
user's `Data/MCM/Settings/OnYourOwn.ini` written by MCM. Unknown keys and bad values are ignored.
They are reloaded when the Pip-Boy opens and when the pause menu (where MCM lives) closes.

`tests/defaults_test.cpp` checks that `settings.ini` matches the built-in defaults.

## Pip-Boy map

`PipboyMapMenu::UpdateData` (vtable slot 2) is hooked. The filter runs before and after the
original, in two passes.

**Tree pass.** `RE::PipboyDataManager::mapData` holds the Pip-Boy data tree. Location, custom
waypoint and power armor markers have a boolean `Visible` member, which is set per setting.
Changed values are remembered and restored when the marker should show again. Settlements are
recognised by marker type, or by the `LocTypeWorkshopSettlement` keyword of their location (for
settlements with their own icon, such as Sanctuary).

**Flash pass.** Quest and player markers have no `Visible` member. `Pipboy_MapPage.swf` reads its
markers from `dataObj.WorldMapMarkers` / `LocalMapMarkers`, which the game rebuilds on every update.
Right after that, hidden entries are removed from the new array, so the SWF never creates their
clips: nothing is drawn, hovered or selected. The SWF matches selection, centering and fast travel
by each entry's `arrayIndex`, not by position, so removing entries is safe.

- Entry types are mapped to clip classes from the vanilla SWF table (`QuestMarker`,
  `QuestMarkerDoor`, `PlayerLocMarker`); the class of a real clip wins if a replaced SWF differs.
- An entry's `arrayIndex` equals the tree object's `PipboyValue::m_id`. That links the passes:
  quest groups come from the tree quest marker's `QuestId` array (`TESQuest::data.questType`;
  Miscellaneous and None count as misc), and hidden locations are also removed here, because the
  game itself sets some quest-related locations visible again during `UpdateData`.

**Hidden player.** Without a player marker the world map opens at its top-left corner. Instead it
is zoomed out (`mapScale = 0`) and centered. The pan limits are `protected` in the SWF, so the range
is found by calling `MoveMap` far past each edge (it clamps) and then moving to the middle. This
runs every UI frame (`UIAdvanceMenusFunctionCompleteEvent`), once per Pip-Boy open and whenever the
map page becomes visible again. The local map keeps the game's own view.

Re-queueing an F4SE UI task from inside itself instead of using the frame event froze the game.

## HUD

`HUDMenu.swf` (vanilla and HUDFramework are identical here) has no ActionScript for compass
markers; the game drives the clips directly. Hiding runs every UI frame and only restores clips
it hid itself. With every HUD option on, the HUD is not touched at all, so iHUD and similar mods
keep full control.

**Compass.** `root.BottomCenterGroup_mc.CompassWidget_mc` holds `QuestMarkerHolder_mc` and
`OtherMarkerHolder_mc`. Each marker is a `CompassMarkerWidget` whose frame label is its category:
`Quest`, `QuestDoor`, `PlayerSet`, `Recon`, `EnemyTargeted`, `Enemy`, `Location`. Location markers
contain an icon clip labelled `Discovered` or `Undiscovered`. The N/E/S/W letters are
`DirectionMarkerWidget`s. The game sets each marker's `visible` flag and the icon's alpha, but never
the marker's own alpha, so hidden markers get alpha 0. "Show compass" off sets the whole widget's
alpha to 0.

**Floating markers.** The markers drawn over targets in the world are `FloatingTarget` clips (same
frame labels) under `root.FloatingQuestMarkerBase`. The game fades a `FloatingTarget`'s own alpha
every frame, so the icon inside it (`FloatingTargetSubClip`) is hidden instead.

## Safety checks

The plugin stays inactive, or the affected part disables itself, if any of these fail:

1. The old-gen DLL loads only on game 1.10.163, the other one only on 1.11.240 (F4SE checks it).
2. The `PipboyMapMenu` vtable slot holds exactly the `UpdateData` address from the Address Library.
3. `PipboyDataManager::mapData` has the `PipboyMapData` vtable.
4. The cached map menu and its containing `PipboyMenu` have the expected vtables.
5. Tree members exist and have the expected type; SWF arrays and members exist.
6. Any exception disables the part that threw it and leaves the game as it was.

## Diagnostics

The MCM "Diagnostic logging" dropdown writes details for one area to
`Documents/My Games/Fallout4/F4SE/OnYourOwn.log`. Lines look like
`[12:34:56] [tag] message`; tags are `plugin`, `settings`, `location`, `quest`, `map`,
`map_view`, `compass`, `warning` and `error`. Summaries are written only when they change.
Settings, warnings and errors are always logged.

- `[map] world map markers=.. hidden=.. ...`: the SWF marker lists after filtering.
  `quest_group_unknown` and `failed_to_hide` should not appear.
- `[compass] compass shown, hidden: ...`: HUD markers hidden per category. `re-shown by the game`
  should not appear (it would mean the game undoes the hiding).
- `[compass] on compass: ...` / `in world: ...`: which markers exist. `unknown (class ..., frame ...)`
  points at a marker type the mod does not know yet.
