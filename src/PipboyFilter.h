#pragma once

namespace OnYourOwn::Pipboy
{
    // Loads the settings, hooks the Pip-Boy map and registers the UI listeners that also drive
    // the HUD marker hiding. Call once at kGameDataReady. Returns false and leaves the game
    // untouched if the runtime does not match what this build expects.
    bool Install();

    // Re-reads Data/MCM/Config/OnYourOwn/settings.ini and the user's MCM overrides.
    void ReloadSettings();
}
