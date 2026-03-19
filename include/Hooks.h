#pragma once

namespace Fixes
{
    void Install();

    namespace NordRaceStats
    {
        void Install();
    }

    namespace ScaleMovementSpeed
    {
        void Install();

        float Call(RE::TESObjectREFR* a_ref);

        inline REL::Relocation<decltype(&Call)> Callback;
    }
}

namespace Tweaks
{
    void Install();

    namespace AbsorptionChance
    {
        std::uint32_t Call(RE::ActorValueOwner* a_owner, RE::ActorValue a_akValue);
    }

    namespace ConcentrationCasting
    {
        bool Call(RE::Actor* a_actor, RE::ActorValue a_akValue, RE::MagicItem* a_spell,
                  float a_cost, bool a_usePermanent);
    }

    namespace JumpHeight
    {
        void Install();

        float GetScale(RE::TESObjectREFR* a_ref);

        inline REL::Relocation<decltype(&GetScale)> OriginalGetScale;
    }
}
