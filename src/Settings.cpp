#include "PCH.h"
#include "Settings.h"

namespace Settings
{
    namespace
    {
        void ReadBool(CSimpleIniA& a_ini, const char* a_section, const char* a_key, bool& a_value)
        {
            a_value = a_ini.GetBoolValue(a_section, a_key, a_value);
            logger::info("  {} = {}", a_key, a_value);
        }

        void ReadFloat(CSimpleIniA& a_ini, const char* a_section, const char* a_key, float& a_value)
        {
            a_value = static_cast<float>(a_ini.GetDoubleValue(a_section, a_key, a_value));
            logger::info("  {} = {}", a_key, a_value);
        }
    }

    void Load()
    {
        logger::info("DLLTweaksVR: Loading settings...");

        auto path = std::format("Data/SKSE/Plugins/{}.ini", Plugin::NAME);

        CSimpleIniA ini;
        ini.SetUnicode();
        auto rc = ini.LoadFile(path.c_str());
        if (rc < 0) {
            logger::warn("Failed to load INI at '{}', using defaults", path);
            return;
        }

        logger::info("[Fixes]");
        ReadBool(ini, "Fixes", "bNordRaceStats", NordRaceStats);
        ReadBool(ini, "Fixes", "bScaleMovementSpeed", ScaleMovementSpeed);

        logger::info("[Tweaks]");
        ReadBool(ini, "Tweaks", "bAbsorptionChance", AbsorptionChance);
        ReadBool(ini, "Tweaks", "bConcentrationCasting", ConcentrationCasting);
        ReadBool(ini, "Tweaks", "bSneakJumpHeight", SneakJumpHeight);
        ReadFloat(ini, "Tweaks", "fSneakJumpModifier", SneakJumpHeightMod);

        logger::info("DLLTweaksVR: Settings loaded!");
    }
}
