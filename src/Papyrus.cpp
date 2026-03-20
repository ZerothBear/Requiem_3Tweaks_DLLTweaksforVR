#include "PCH.h"

#include "Hooks.h"
#include "Papyrus.h"
#include "Settings.h"

namespace Papyrus
{
    namespace
    {
        void ReloadSettings([[maybe_unused]] RE::TESQuest* a_self)
        {
            const auto nordWasEnabled = Settings::NordRaceStats.load(std::memory_order_relaxed);

            Settings::Load();

            const auto nordIsEnabled = Settings::NordRaceStats.load(std::memory_order_relaxed);
            if (!nordWasEnabled && nordIsEnabled) {
                logger::info("Papyrus: NordRaceStats enabled from MCM; applying one-shot fix immediately");
                Fixes::NordRaceStats::Install();
            } else if (nordWasEnabled && !nordIsEnabled) {
                logger::warn("Papyrus: NordRaceStats was disabled from MCM; a restart is required to fully revert the one-shot fix");
            }
        }
    }

    bool Register(RE::BSScript::IVirtualMachine* a_vm)
    {
        a_vm->RegisterFunction("ReloadSettings"sv, McmScriptName, ReloadSettings);
        logger::info("Papyrus: registered native bindings for {}", McmScriptName);
        return true;
    }
}
