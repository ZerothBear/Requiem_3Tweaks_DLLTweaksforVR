#include "PCH.h"
#include "Hooks.h"
#include "Settings.h"

namespace
{
    void InitLogger()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            stl::report_and_fail("Failed to find SKSE log directory"sv);
        }

        *path /= std::format("{}.log", Plugin::NAME);

        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));

        if (IsDebuggerPresent()) {
            sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
        }

        auto log = std::make_shared<spdlog::logger>("Global", sinks.begin(), sinks.end());
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        if (a_message->type == SKSE::MessagingInterface::kDataLoaded) {
            logger::info("kDataLoaded received — installing hooks");
            Settings::Load();
            Fixes::Install();
            Tweaks::Install();
            Fixes::NordRaceStats::Install();
            logger::info("All hooks processed");
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
    InitLogger();

    logger::info("{} v{} loaded", Plugin::NAME, Plugin::VERSION.string());
    logger::info("Runtime: SkyrimVR");

    SKSE::Init(a_skse, false);

    SKSE::AllocTrampoline(256);

    if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
        stl::report_and_fail("Unable to register message listener."sv);
    }

    logger::info("Message listener registered — waiting for kDataLoaded");

    return true;
}
