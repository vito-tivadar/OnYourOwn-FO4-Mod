# Contributing

Read [docs/how-it-works.md](docs/how-it-works.md) first.

## Ground rules

- **Never change persistent game state**: discovery, quests, fast travel, map marker forms,
  player position or anything stored in saves. The plugin only filters what the UI displays.
- **Fail open.** Anything not positively recognised stays visible. A failed check disables the
  feature and leaves the game as it was; it must never crash or hide markers by accident.
- **Do not guess game internals.** Offsets, vtable slots and class layouts must come from
  CommonLibF4 headers or the Address Library, and new hooks need a runtime guard.
- **Keep game logic testable.** "Should this marker show?" decisions belong in the pure files
  (`MarkerPolicy`, `CompassPolicy`, `MarkerClassifier`, `IniSettings`) with tests in `tests/`.
- **No SWF replacements.** The mod must work alongside HUD and Pip-Boy mods.
- Compiling is not testing: say what was checked in game and what was not.

## Adding a setting

1. Add the field and its default to `Settings` or `CompassSettings`.
2. Bind its INI key in `src/IniSettings.cpp` (`"key:Section"` must match the MCM `id`).
3. Add it to `Data/MCM/Config/OnYourOwn/config.json` and `settings.ini` (same default).
4. Add tests; `defaults_test` fails if `settings.ini` and the code disagree.
