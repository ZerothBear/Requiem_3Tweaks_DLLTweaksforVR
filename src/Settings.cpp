#include "PCH.h"
#include "Settings.h"

namespace Settings
{
    namespace
    {
        struct Snapshot
        {
            bool debugging{ false };
            bool nordRaceStats{ false };
            bool scaleMovementSpeed{ false };
            bool absorptionChance{ false };
            bool concentrationCasting{ false };
            bool sneakJumpHeight{ false };
            float sneakJumpHeightMod{ 0.0f };
            float absorptionChanceCap{ 0.0f };
        };

        constexpr float kSneakJumpHeightMin = 0.0f;
        constexpr float kSneakJumpHeightMax = 1.0f;
        constexpr float kAbsorptionChanceCapMin = 0.0f;
        constexpr float kAbsorptionChanceCapMax = 100.0f;

        void MergeIni(const CSimpleIniA& a_source, CSimpleIniA& a_target)
        {
            std::list<CSimpleIniA::Entry> sections;
            a_source.GetAllSections(sections);

            for (const auto& section : sections) {
                std::list<CSimpleIniA::Entry> keys;
                a_source.GetAllKeys(section.pItem, keys);

                for (const auto& key : keys) {
                    a_target.SetValue(
                        section.pItem,
                        key.pItem,
                        a_source.GetValue(section.pItem, key.pItem),
                        nullptr,
                        true);
                }
            }
        }

        bool OverlayFile(CSimpleIniA& a_target, const std::filesystem::path& a_path, std::string_view a_label)
        {
            logger::info("  {}: {}", a_label, a_path.string());

            if (!std::filesystem::exists(a_path)) {
                logger::info("    not present");
                return false;
            }

            CSimpleIniA overlay;
            overlay.SetUnicode();

            if (overlay.LoadFile(a_path.string().c_str()) < 0) {
                logger::warn("    failed to parse");
                return false;
            }

            MergeIni(overlay, a_target);
            logger::info("    loaded");
            return true;
        }

        bool ReadBool(CSimpleIniA& a_ini, const char* a_section, const char* a_key, bool a_verbose)
        {
            const auto value = a_ini.GetBoolValue(a_section, a_key, false);
            if (a_verbose) {
                logger::info("  {} = {}", a_key, value);
            }
            return value;
        }

        float ReadFloat(CSimpleIniA& a_ini, const char* a_section, const char* a_key, bool a_verbose)
        {
            const auto value = static_cast<float>(a_ini.GetDoubleValue(a_section, a_key, 0.0));
            if (a_verbose) {
                logger::info("  {} = {}", a_key, value);
            }
            return value;
        }

        float ClampWithLog(float a_value, float a_min, float a_max, const char* a_key)
        {
            const auto clamped = (std::clamp)(a_value, a_min, a_max);
            if (clamped != a_value) {
                logger::warn("  {} out of range ({}), clamped to {}", a_key, a_value, clamped);
            }

            return clamped;
        }

        Snapshot ReadSnapshot(CSimpleIniA& a_ini, bool a_verbose)
        {
            Snapshot snapshot{};

            if (a_verbose) {
                logger::info("[Debug]");
            }
            snapshot.debugging = ReadBool(a_ini, "Debug", "bDebugging", a_verbose);

            if (a_verbose) {
                logger::info("[Fixes]");
            }
            snapshot.nordRaceStats = ReadBool(a_ini, "Fixes", "bNordRaceStats", a_verbose);
            snapshot.scaleMovementSpeed = ReadBool(a_ini, "Fixes", "bScaleMovementSpeed", a_verbose);

            if (a_verbose) {
                logger::info("[Tweaks]");
            }
            snapshot.absorptionChance = ReadBool(a_ini, "Tweaks", "bAbsorptionChance", a_verbose);
            snapshot.absorptionChanceCap = ClampWithLog(
                ReadFloat(a_ini, "Tweaks", "fAbsorptionChanceCap", a_verbose),
                kAbsorptionChanceCapMin,
                kAbsorptionChanceCapMax,
                "fAbsorptionChanceCap");
            snapshot.concentrationCasting = ReadBool(a_ini, "Tweaks", "bConcentrationCasting", a_verbose);
            snapshot.sneakJumpHeight = ReadBool(a_ini, "Tweaks", "bSneakJumpHeight", a_verbose);
            snapshot.sneakJumpHeightMod = ClampWithLog(
                ReadFloat(a_ini, "Tweaks", "fSneakJumpModifier", a_verbose),
                kSneakJumpHeightMin,
                kSneakJumpHeightMax,
                "fSneakJumpModifier");

            return snapshot;
        }

        void ApplySnapshot(const Snapshot& a_snapshot)
        {
            Debugging.store(a_snapshot.debugging, std::memory_order_relaxed);
            NordRaceStats.store(a_snapshot.nordRaceStats, std::memory_order_relaxed);
            ScaleMovementSpeed.store(a_snapshot.scaleMovementSpeed, std::memory_order_relaxed);
            AbsorptionChance.store(a_snapshot.absorptionChance, std::memory_order_relaxed);
            AbsorptionChanceCap.store(a_snapshot.absorptionChanceCap, std::memory_order_relaxed);
            ConcentrationCasting.store(a_snapshot.concentrationCasting, std::memory_order_relaxed);
            SneakJumpHeight.store(a_snapshot.sneakJumpHeight, std::memory_order_relaxed);
            SneakJumpHeightMod.store(a_snapshot.sneakJumpHeightMod, std::memory_order_relaxed);
        }

        void PersistAsUserSettings(const CSimpleIniA& a_mergedIni)
        {
            const auto path = GetUserSettingsPath();

            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                logger::warn("Failed to create MCM settings directory '{}': {}",
                    path.parent_path().string(), ec.message());
                return;
            }

            if (a_mergedIni.SaveFile(path.string().c_str()) < 0) {
                logger::warn("Failed to save migrated MCM settings to '{}'", path.string());
                return;
            }

            logger::info("Legacy SKSE INI migrated to '{}'", path.string());
        }
    }

    std::filesystem::path GetDefaultSettingsPath()
    {
        return std::filesystem::path("Data/MCM/Config") / Plugin::NAME / "settings.ini";
    }

    std::filesystem::path GetUserSettingsPath()
    {
        return std::filesystem::path("Data/MCM/Settings") / std::format("{}.ini", Plugin::NAME);
    }

    std::filesystem::path GetLegacySettingsPath()
    {
        return std::filesystem::path("Data/SKSE/Plugins") / std::format("{}.ini", Plugin::NAME);
    }

    void Load()
    {
        logger::info("DLLTweaksVR: Loading settings from MCM Helper paths...");

        CSimpleIniA mergedIni;
        mergedIni.SetUnicode();

        const auto defaultPath = GetDefaultSettingsPath();
        const auto userPath = GetUserSettingsPath();
        const auto legacyPath = GetLegacySettingsPath();

        if (!OverlayFile(mergedIni, defaultPath, "defaults")) {
            logger::error("No default settings were loaded; leaving runtime settings fail-closed");
            return;
        }

        const auto loadedUserOverrides = OverlayFile(mergedIni, userPath, "user overrides");
        if (!loadedUserOverrides && std::filesystem::exists(legacyPath)) {
            logger::info("No MCM user override file found; attempting legacy SKSE INI migration");
            if (OverlayFile(mergedIni, legacyPath, "legacy SKSE INI")) {
                PersistAsUserSettings(mergedIni);
            }
        }

        const auto debugLogging = mergedIni.GetBoolValue("Debug", "bDebugging", false);
        ApplySnapshot(ReadSnapshot(mergedIni, debugLogging));
        logger::info("DLLTweaksVR: Settings loaded! Debugging {}", debugLogging ? "enabled" : "disabled");
    }
}
