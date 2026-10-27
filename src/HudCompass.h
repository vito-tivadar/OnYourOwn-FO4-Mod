#pragma once

#include "IniSettings.h"

namespace OnYourOwn::Compass
{
    // Called every UI frame. Hides HUD compass and floating markers per a_config.compass and
    // restores only what it hid; with every option on, the HUD is not touched.
    void OnFrame(const Config& a_config);
}
