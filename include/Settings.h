#pragma once

namespace Settings
{
    // [Fixes]
    inline bool NordRaceStats{ false };
    inline bool ScaleMovementSpeed{ true };

    // [Tweaks]
    inline bool AbsorptionChance{ true };
    inline bool ConcentrationCasting{ true };
    inline bool SneakJumpHeight{ false };
    inline float SneakJumpHeightMod{ 0.55f };

    void Load();
}
