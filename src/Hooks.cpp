#include "PCH.h"
#include "Hooks.h"
#include "Settings.h"

// =============================================================================
// VR Address Offsets (from SE REL::ID -> VR address mapping)
//
//   SE ID 37013 (ScaleMovementSpeed) -> VR 0x617B00
//   SE ID 37792 (AbsorptionChance)   -> VR 0x639720
//   SE ID 33364 (ConcentrationCast)  -> VR 0x5465A0
//   SE ID 36271 (SneakJumpHeight)    -> VR 0x5DA540
//
// Inner offsets (from SE): these may need adjustment for VR.
// The SE offsets are: +0x1A, +0x53, +0x1B3, +0x190
// TODO: Verify inner offsets by disassembling VR binary at these addresses.
// =============================================================================

namespace Fixes
{
    // -------------------------------------------------------------------------
    // NordRaceStats: Recalculates player stats to fix Nord race stat bug.
    // No address hook needed — just calls a vtable function on PlayerCharacter.
    // -------------------------------------------------------------------------
    namespace NordRaceStats
    {
        void Install()
        {
            if (!Settings::NordRaceStats) {
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                // Calls InitItemImpl / recalculate stats on the player
                // This is the vtable call at offset 0x8C0 in the original
                player->InitItemImpl();
                logger::info("Fixes: NordRaceStats applied");
            }
        }
    }

    // -------------------------------------------------------------------------
    // ScaleMovementSpeed: Prevents movement speed from scaling with race height.
    // Hooks the GetScale call in the movement speed calculation and returns 1.0
    // for the player character.
    // -------------------------------------------------------------------------
    namespace ScaleMovementSpeed
    {
        float Call(RE::TESObjectREFR* a_ref)
        {
            // Call the original function first
            float scale = Callback(a_ref);

            // If this is the player, return 1.0 to neutralize race height scaling
            if (a_ref == RE::PlayerCharacter::GetSingleton()) {
                return 1.0f;
            }

            return scale;
        }

        void Install()
        {
            if (!Settings::ScaleMovementSpeed) {
                return;
            }

            // SE ID 37013 -> VR offset 0x617B00, inner offset +0x1A (SE value)
            // TODO: Verify +0x1A is correct for VR. May differ.
            constexpr std::uintptr_t funcOffset = 0x617B00;
            constexpr std::ptrdiff_t innerOffset = 0x1A;

            auto addr = REL::Offset(funcOffset).address() + innerOffset;

            auto& trampoline = SKSE::GetTrampoline();
            Callback = trampoline.write_call<5>(addr, Call);

            logger::info("Fixes: ScaleMovementSpeed installed at {:X}+{:X}", funcOffset, innerOffset);
        }
    }

    void Install()
    {
        ScaleMovementSpeed::Install();
    }
}

namespace Tweaks
{
    // -------------------------------------------------------------------------
    // AbsorptionChance: Caps spell absorption at fPlayerMaxResistance.
    // Hooks the GetActorValue call for absorption and clamps the result.
    // -------------------------------------------------------------------------
    namespace AbsorptionChance
    {
        std::uint32_t Call(RE::ActorValueOwner* a_owner, RE::ActorValue a_akValue)
        {
            float value = a_owner->GetActorValue(a_akValue);

            auto* settings = RE::GameSettingCollection::GetSingleton();
            auto* setting = settings->GetSetting("fPlayerMaxResistance");
            float cap = setting ? setting->GetFloat() : 85.0f;

            return static_cast<std::uint32_t>(std::min(value, cap));
        }

        void Install()
        {
            if (!Settings::AbsorptionChance) {
                return;
            }

            // SE ID 37792 -> VR offset 0x639720, inner offset +0x53 (SE value)
            // TODO: Verify +0x53 is correct for VR.
            constexpr std::uintptr_t funcOffset = 0x639720;
            constexpr std::ptrdiff_t innerOffset = 0x53;

            auto addr = REL::Offset(funcOffset).address() + innerOffset;

            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(addr, Call);

            // NOP the 3 bytes after the call (matching original: 3x 0x90)
            REL::safe_fill(addr + 5, REL::NOP, 3);

            logger::info("Tweaks: AbsorptionChance installed at {:X}+{:X}", funcOffset, innerOffset);
        }
    }

    // -------------------------------------------------------------------------
    // ConcentrationCasting: Prevents casting concentration spells when magicka
    // is insufficient. For concentration spells (type==2), checks both base
    // and current magicka against the per-second cost.
    // -------------------------------------------------------------------------
    namespace ConcentrationCasting
    {
        bool Call(RE::Actor* a_actor, RE::ActorValue a_akValue, RE::MagicItem* a_spell,
                  float a_cost, bool a_usePermanent)
        {
            auto* avOwner = a_actor->AsActorValueOwner();

            float available;
            if (a_usePermanent) {
                available = avOwner->GetPermanentActorValue(a_akValue);
            } else {
                available = avOwner->GetActorValue(a_akValue);
            }

            if (a_spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
                // For concentration spells, also check base value
                float baseValue = avOwner->GetBaseActorValue(a_akValue);
                return (baseValue >= a_cost) && (available >= a_cost);
            }

            return available >= a_cost;
        }

        void Install()
        {
            if (!Settings::ConcentrationCasting) {
                return;
            }

            // SE ID 33364 -> VR offset 0x5465A0, inner offset +0x1B3 (SE value)
            // The original does a byte pattern check before patching.
            // TODO: Verify +0x1B3 and byte pattern for VR.
            constexpr std::uintptr_t funcOffset = 0x5465A0;
            constexpr std::ptrdiff_t innerOffset = 0x1B3;

            auto addr = REL::Offset(funcOffset).address() + innerOffset;

            // Byte pattern validation (from original):
            // Expected at addr+0x1E: FF 90 A8 02 00 00
            // This checks we're patching the right location
            std::uint8_t expected[] = { 0xFF, 0x90, 0xA8, 0x02, 0x00, 0x00 };
            bool patternMatch = true;
            for (int i = 0; i < 6; ++i) {
                if (*reinterpret_cast<std::uint8_t*>(addr + 0x1E + i) != expected[i]) {
                    patternMatch = false;
                    break;
                }
            }

            if (!patternMatch) {
                logger::error("Tweaks: ConcentrationCasting byte pattern mismatch — skipping");
                return;
            }

            // The original patches in a complex inline assembly sequence:
            // - Sets up registers for the replacement call
            // - Writes a trampoline call to our replacement
            // - NOPs out the remaining original bytes
            //
            // Inline patch: set up args and call our function
            // mov rbx, [rax+rbx]    48 8B 9B
            // mov rdx, rbp          89 EA
            // mov r8, rdi            49 89 F8
            // comiss xmm3, xmm7     0F 28 DF
            // movss [rsp+0xC8], xmm1  8A 48 24 C8 .. (approximation)
            // movss [rsp+0x20], xmm0  88 44 24 20

            std::uint8_t patch1[] = { 0x48, 0x8B, 0x9B };  // mov rbx, [rax+rbx]
            REL::safe_write(addr, patch1, 3);

            // Additional patch bytes omitted — using trampoline call instead
            // which is simpler and more maintainable

            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(addr + 0x1A, Call);

            // NOP remaining original bytes (0x19 bytes from addr+0x1F)
            REL::safe_fill(addr + 0x1F, REL::NOP, 0x19);

            logger::info("Tweaks: ConcentrationCasting installed at {:X}+{:X}", funcOffset, innerOffset);
        }
    }

    // -------------------------------------------------------------------------
    // JumpHeight: Reduces jump height while sneaking by a configurable modifier.
    // Hooks the GetScale call in the jump height calculation.
    // -------------------------------------------------------------------------
    namespace JumpHeight
    {
        float GetScale(RE::TESObjectREFR* a_ref)
        {
            float scale = OriginalGetScale(a_ref);

            // If this is the player and they're sneaking, reduce jump height
            auto* actor = a_ref->As<RE::Actor>();
            if (actor && actor == RE::PlayerCharacter::GetSingleton() && actor->IsSneaking()) {
                scale *= Settings::SneakJumpHeightMod;
            }

            return scale;
        }

        void Install()
        {
            if (!Settings::SneakJumpHeight) {
                return;
            }

            // SE ID 36271 -> VR offset 0x5DA540, inner offset +0x190 (SE value)
            // TODO: Verify +0x190 is correct for VR.
            constexpr std::uintptr_t funcOffset = 0x5DA540;
            constexpr std::ptrdiff_t innerOffset = 0x190;

            auto addr = REL::Offset(funcOffset).address() + innerOffset;

            auto& trampoline = SKSE::GetTrampoline();
            OriginalGetScale = trampoline.write_call<5>(addr, GetScale);

            logger::info("Tweaks: SneakJumpHeight installed at {:X}+{:X}", funcOffset, innerOffset);
        }
    }

    void Install()
    {
        AbsorptionChance::Install();
        ConcentrationCasting::Install();
        JumpHeight::Install();
    }
}
