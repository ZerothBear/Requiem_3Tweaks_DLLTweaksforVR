#include "PCH.h"
#include "Hooks.h"
#include "Settings.h"

namespace
{
    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        if (a_message->type == SKSE::MessagingInterface::kDataLoaded) {
            Settings::Load();
            Fixes::Install();
            Tweaks::Install();
            Fixes::NordRaceStats::Install();
        }
    }
}

SKSEPluginInfo(
        .Version = Plugin::VERSION,
        .Name = Plugin::NAME.data(),
        .Author = "spyde",
        .StructCompatibility = SKSE::StructCompatibility::Independent,
        .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary);

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);

    logger::info("{} v{} loaded", Plugin::NAME, Plugin::VERSION.string());

    SKSE::AllocTrampoline(256);

    if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
        stl::report_and_fail("Unable to register message listener."sv);
    }

    return true;
}
