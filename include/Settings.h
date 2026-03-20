#pragma once

namespace Settings
{
    // Runtime state is fail-closed until the MCM settings files are loaded.
    inline std::atomic_bool Debugging{ false };
    inline std::atomic_bool NordRaceStats{ false };
    inline std::atomic_bool ScaleMovementSpeed{ false };
    inline std::atomic_bool AbsorptionChance{ false };
    inline std::atomic_bool ConcentrationCasting{ false };
    inline std::atomic_bool SneakJumpHeight{ false };
    inline std::atomic<float> SneakJumpHeightMod{ 0.0f };
    inline std::atomic<float> AbsorptionChanceCap{ 0.0f };

    [[nodiscard]] std::filesystem::path GetDefaultSettingsPath();
    [[nodiscard]] std::filesystem::path GetUserSettingsPath();
    [[nodiscard]] std::filesystem::path GetLegacySettingsPath();

    void Load();
}
