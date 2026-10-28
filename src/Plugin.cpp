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

#ifdef OYO_OLD_GEN
    // F4SE 0.6's interface ends before GetSaveFolderName, which F4SE::Init reads for the log path.
    const F4SE::LoadInterface* WithSaveFolderName(const F4SE::LoadInterface* a_f4se)
    {
        static F4SE::Impl::F4SEInterface full{};
        std::memcpy(&full, a_f4se, offsetof(F4SE::Impl::F4SEInterface, GetSaveFolderName));
        full.GetSaveFolderName = []() -> const char* { return "Fallout4"; };
        return reinterpret_cast<const F4SE::LoadInterface*>(&full);
    }
#endif
}

#ifdef OYO_OLD_GEN
// F4SE 0.6 (game 1.10.163) loads plugins through Query instead of F4SEPlugin_Version.
F4SE_EXPORT bool F4SEPlugin_Query(const F4SE::QueryInterface* f4se, F4SE::PluginInfo* info)
{
    const auto* plugin = F4SE::PluginVersionData::GetSingleton();
    info->infoVersion = F4SE::PluginInfo::kVersion;
    info->name = plugin->GetPluginName().data();
    info->version = plugin->GetPluginVersion().pack();
    return !f4se->IsEditor() && f4se->RuntimeVersion() == F4SE::RUNTIME_1_10_163;
}
#endif

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* f4se)
{
    if (!f4se || f4se->IsEditor()) {
        return false;
    }
#ifdef OYO_OLD_GEN
    // This build uses 1.10.163 addresses; next-gen F4SE can still load it through F4SEPlugin_Version.
    if (f4se->RuntimeVersion() != F4SE::RUNTIME_1_10_163) {
        return false;
    }
    f4se = WithSaveFolderName(f4se);
#endif

    F4SE::Init(f4se, { .logName = "OnYourOwn", .logPattern = "[%T] %v", .hook = false });
    OnYourOwn::Log::Info("plugin", "game {}, F4SE {}", f4se->RuntimeVersion().string(), f4se->F4SEVersion().string());

    if (const auto* messaging = F4SE::GetMessagingInterface()) {
        messaging->RegisterListener(OnF4SEMessage);
    } else {
        OnYourOwn::Log::Error("F4SE messaging unavailable, mod disabled");
    }
    return true;
}
