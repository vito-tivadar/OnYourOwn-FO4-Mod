#include "PCH.h"

#include "Log.h"
#include "PipboyFilter.h"

namespace
{
    void F4SEAPI OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
    {
        // The Address Library, UI singleton and Pip-Boy classes are all ready by kGameDataReady.
        if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
            if (!OnYourOwn::Pipboy::Install()) {
                OnYourOwn::Log::Warn("mod not active, Pip-Boy map and HUD stay vanilla");
            }
        }
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* f4se)
{
    if (!f4se || f4se->IsEditor()) {
        return false;
    }

    F4SE::Init(f4se, { .logName = "OnYourOwn", .logPattern = "[%T] %v", .hook = false });
    OnYourOwn::Log::Info("plugin", "game {}, F4SE {}", f4se->RuntimeVersion().string(), f4se->F4SEVersion().string());

    if (const auto* messaging = F4SE::GetMessagingInterface()) {
        messaging->RegisterListener(OnF4SEMessage);
    } else {
        OnYourOwn::Log::Error("F4SE messaging unavailable, mod disabled");
    }
    return true;
}
