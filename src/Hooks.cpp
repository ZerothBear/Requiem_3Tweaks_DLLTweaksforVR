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
// Inner offsets from SE decompilation. VR functions may differ in layout.
// All hooks have runtime byte-pattern validation and will skip if mismatched.
// =============================================================================

namespace
{
    // Dump bytes at an address for debugging
    void DumpBytes(const char* label, std::uintptr_t addr, std::size_t count)
    {
        std::string hex;
        for (std::size_t i = 0; i < count; ++i) {
            auto byte = *reinterpret_cast<std::uint8_t*>(addr + i);
            hex += std::format("{:02X} ", byte);
        }
        logger::info("  [DUMP] {} @ {:X}: {}", label, addr, hex);
    }

    // Check if a byte at the address is a CALL (E8) instruction
    bool IsCallInstruction(std::uintptr_t addr)
    {
        return *reinterpret_cast<std::uint8_t*>(addr) == 0xE8;
    }

    // Scan for the nearest CALL (E8) instruction in a range around the expected offset
    // Returns the address of the CALL, or 0 if not found
    std::uintptr_t FindNearestCall(std::uintptr_t baseAddr, std::ptrdiff_t expectedOffset,
                                    std::ptrdiff_t searchRadius = 32)
    {
        auto expected = baseAddr + expectedOffset;

        // Check exact offset first
        if (IsCallInstruction(expected)) {
            logger::info("  [SCAN] Found E8 at expected offset +0x{:X}", expectedOffset);
            return expected;
        }

        // Scan outward from expected position
        for (std::ptrdiff_t delta = 1; delta <= searchRadius; ++delta) {
            if (IsCallInstruction(expected + delta)) {
                logger::warn("  [SCAN] E8 not at +0x{:X}, found at +0x{:X} (delta +{})",
                    expectedOffset, expectedOffset + delta, delta);
                return expected + delta;
            }
            if (IsCallInstruction(expected - delta)) {
                logger::warn("  [SCAN] E8 not at +0x{:X}, found at +0x{:X} (delta -{})",
                    expectedOffset, expectedOffset - delta, delta);
                return expected - delta;
            }
        }

        logger::error("  [SCAN] No E8 found within +/-{} bytes of +0x{:X}", searchRadius, expectedOffset);
        return 0;
    }
}

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
                logger::info("Fixes: NordRaceStats DISABLED by INI");
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                player->InitItemImpl();
                logger::info("Fixes: NordRaceStats applied (recalculated player stats)");
            } else {
                logger::error("Fixes: NordRaceStats FAILED — PlayerCharacter singleton is null");
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
            float scale = Callback(a_ref);

            if (a_ref == RE::PlayerCharacter::GetSingleton()) {
                return 1.0f;
            }

            return scale;
        }

        void Install()
        {
            if (!Settings::ScaleMovementSpeed) {
                logger::info("Fixes: ScaleMovementSpeed DISABLED by INI");
                return;
            }

            constexpr std::uintptr_t funcOffset = 0x617B00;
            constexpr std::ptrdiff_t seInnerOffset = 0x1A;

            auto baseAddr = REL::Offset(funcOffset).address();
            logger::info("Fixes: ScaleMovementSpeed — base addr: {:X}", baseAddr);
            DumpBytes("func prologue", baseAddr, 48);
            DumpBytes("around SE offset", baseAddr + seInnerOffset - 8, 32);

            auto callAddr = FindNearestCall(baseAddr, seInnerOffset);
            if (!callAddr) {
                logger::error("Fixes: ScaleMovementSpeed SKIPPED — could not find CALL instruction");
                DumpBytes("search area", baseAddr, 64);
                return;
            }

            auto& trampoline = SKSE::GetTrampoline();
            Callback = trampoline.write_call<5>(callAddr, Call);

            logger::info("Fixes: ScaleMovementSpeed INSTALLED at {:X} (offset +0x{:X})",
                callAddr, callAddr - baseAddr);
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
    // -------------------------------------------------------------------------
    namespace AbsorptionChance
    {
        std::uint32_t Call(RE::ActorValueOwner* a_owner, RE::ActorValue a_akValue)
        {
            float value = a_owner->GetActorValue(a_akValue);

            auto* gmst = RE::GameSettingCollection::GetSingleton();
            auto* setting = gmst ? gmst->GetSetting("fPlayerMaxResistance") : nullptr;
            float cap = setting ? setting->GetFloat() : 85.0f;

            return static_cast<std::uint32_t>((std::min)(value, cap));
        }

        void Install()
        {
            if (!Settings::AbsorptionChance) {
                logger::info("Tweaks: AbsorptionChance DISABLED by INI");
                return;
            }

            constexpr std::uintptr_t funcOffset = 0x639720;
            constexpr std::ptrdiff_t seInnerOffset = 0x53;

            auto baseAddr = REL::Offset(funcOffset).address();
            logger::info("Tweaks: AbsorptionChance — base addr: {:X}", baseAddr);
            DumpBytes("func prologue", baseAddr, 48);
            DumpBytes("around SE offset", baseAddr + seInnerOffset - 8, 32);

            auto callAddr = FindNearestCall(baseAddr, seInnerOffset);
            if (!callAddr) {
                logger::error("Tweaks: AbsorptionChance SKIPPED — could not find CALL instruction");
                DumpBytes("search area", baseAddr + seInnerOffset - 32, 80);
                return;
            }

            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(callAddr, Call);

            // NOP the 3 bytes after the call (matching original SE patch)
            REL::safe_fill(callAddr + 5, REL::NOP, 3);

            logger::info("Tweaks: AbsorptionChance INSTALLED at {:X} (offset +0x{:X})",
                callAddr, callAddr - baseAddr);
        }
    }

    // -------------------------------------------------------------------------
    // ConcentrationCasting: Prevents casting concentration spells when magicka
    // is insufficient.
    // -------------------------------------------------------------------------
    namespace ConcentrationCasting
    {
        bool Call(RE::ActorValueOwner* a_avOwner, RE::ActorValue a_akValue,
                  RE::MagicItem* a_spell, float a_cost, bool a_usePermanent)
        {
            float available;
            if (a_usePermanent) {
                available = a_avOwner->GetPermanentActorValue(a_akValue);
            } else {
                available = a_avOwner->GetActorValue(a_akValue);
            }

            if (a_spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
                float baseValue = a_avOwner->GetBaseActorValue(a_akValue);
                return (baseValue >= a_cost) && (available >= a_cost);
            }

            return available >= a_cost;
        }

        void Install()
        {
            if (!Settings::ConcentrationCasting) {
                logger::info("Tweaks: ConcentrationCasting DISABLED by INI");
                return;
            }

            constexpr std::uintptr_t funcOffset = 0x5465A0;
            constexpr std::ptrdiff_t seInnerOffset = 0x1B3;

            auto baseAddr = REL::Offset(funcOffset).address();
            logger::info("Tweaks: ConcentrationCasting — base addr: {:X}", baseAddr);
            DumpBytes("func prologue", baseAddr, 48);

            // Find the patch site by scanning for the GetSpellType virtual call
            // pattern: FF 90 A8 02 00 00 = call qword ptr [rax+0x2A8]
            // In VR this should be at patchSite + 0x1E (with possible small delta)
            auto patchSite = baseAddr + seInnerOffset;
            DumpBytes("SE patch site", patchSite - 8, 96);

            bool foundPattern = false;
            std::ptrdiff_t patternDelta = 0;

            for (std::ptrdiff_t scan = -64; scan <= 64; ++scan) {
                auto scanAddr = patchSite + 0x1E + scan;
                if (*reinterpret_cast<std::uint8_t*>(scanAddr) == 0xFF &&
                    *reinterpret_cast<std::uint8_t*>(scanAddr + 1) == 0x90 &&
                    *reinterpret_cast<std::uint32_t*>(scanAddr + 2) == 0x000002A8) {
                    foundPattern = true;
                    patternDelta = scan;
                    logger::info("  [SCAN] Found FF 90 A8 02 00 00 pattern at delta {} from expected",
                        patternDelta);
                    break;
                }
            }

            if (!foundPattern) {
                logger::error("Tweaks: ConcentrationCasting SKIPPED — "
                    "cannot find call [rax+0x2A8] pattern near +0x{:X}", seInnerOffset);
                DumpBytes("wide scan area", patchSite - 32, 128);
                return;
            }

            // The adjusted site starts at the "cmp byte ptr [rsp+0xD8], 0" instruction
            // which begins the entire comparison block we're replacing.
            auto adjustedSite = patchSite + patternDelta;
            logger::info("  Adjusted patch site: {:X} (delta {})", adjustedSite, patternDelta);
            DumpBytes("adjusted patch site", adjustedSite, 64);

            // Validate that the patch site starts with cmp byte ptr [rsp+0xD8], 0
            // Expected: 80 BC 24 D8 00 00 00 00
            if (*reinterpret_cast<std::uint8_t*>(adjustedSite) != 0x80 ||
                *reinterpret_cast<std::uint8_t*>(adjustedSite + 1) != 0xBC ||
                *reinterpret_cast<std::uint8_t*>(adjustedSite + 2) != 0x24) {
                logger::error("Tweaks: ConcentrationCasting SKIPPED — "
                    "unexpected instruction at patch site (expected cmp [rsp+...])");
                return;
            }

            // ---------------------------------------------------------------
            // VR register state at the patch site:
            //   rcx = ActorValueOwner*     → param 1 (already correct)
            //   edx = ActorValue enum      → param 2 (already correct)
            //   rdi = MagicItem*           → param 3 (needs mov r8, rdi)
            //   xmm7 = cost (float)        → param 4 (needs movaps xmm3, xmm7)
            //   [rsp+0xD8] = usePermanent  → param 5 (load to al, store to [rsp+0x20])
            //
            // We replace 0x38 (56) bytes: the entire comparison block from
            // "cmp byte ptr [rsp+0xD8], 0" through "setae al", leaving the
            // subsequent "movaps xmm6, [rsp+0x60]; mov [rsp+??], al" intact
            // to restore xmm6 and store our bool result from al.
            // ---------------------------------------------------------------

            constexpr std::size_t kPatchRegionSize = 0x38; // 56 bytes

            // Build the inline setup + call
            std::uint8_t setup[] = {
                0x49, 0x89, 0xF8,                               // mov r8, rdi           (MagicItem*)
                0x0F, 0x28, 0xDF,                               // movaps xmm3, xmm7    (cost)
                0x8A, 0x84, 0x24, 0xD8, 0x00, 0x00, 0x00,      // mov al, [rsp+0xD8]   (usePermanent)
                0x88, 0x44, 0x24, 0x20,                         // mov [rsp+0x20], al   (5th param)
            };
            constexpr std::size_t kSetupSize = sizeof(setup);   // 17 bytes
            constexpr std::size_t kCallSize = 5;                // E8 + rel32
            constexpr std::size_t kNopSize = kPatchRegionSize - kSetupSize - kCallSize; // 34 bytes

            // Write setup bytes
            REL::safe_write(adjustedSite, setup, kSetupSize);

            // Write trampoline call to our ConcentrationCasting::Call
            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(adjustedSite + kSetupSize, Call);

            // NOP the remaining bytes
            REL::safe_fill(adjustedSite + kSetupSize + kCallSize, REL::NOP, kNopSize);

            logger::info("Tweaks: ConcentrationCasting INSTALLED at {:X} (adjusted offset +0x{:X})",
                adjustedSite, adjustedSite - baseAddr);
            logger::info("  Patch layout: {} setup + {} call + {} NOP = {} bytes",
                kSetupSize, kCallSize, kNopSize, kPatchRegionSize);
        }
    }

    // -------------------------------------------------------------------------
    // JumpHeight: Reduces jump height while sneaking by a configurable modifier.
    // -------------------------------------------------------------------------
    namespace JumpHeight
    {
        float GetScale(RE::TESObjectREFR* a_ref)
        {
            float scale = OriginalGetScale(a_ref);

            auto* actor = a_ref->As<RE::Actor>();
            if (actor && actor == RE::PlayerCharacter::GetSingleton() && actor->IsSneaking()) {
                scale *= Settings::SneakJumpHeightMod;
            }

            return scale;
        }

        void Install()
        {
            if (!Settings::SneakJumpHeight) {
                logger::info("Tweaks: SneakJumpHeight DISABLED by INI");
                return;
            }

            constexpr std::uintptr_t funcOffset = 0x5DA540;
            constexpr std::ptrdiff_t seInnerOffset = 0x190;

            auto baseAddr = REL::Offset(funcOffset).address();
            logger::info("Tweaks: SneakJumpHeight — base addr: {:X}", baseAddr);
            DumpBytes("func prologue", baseAddr, 48);
            DumpBytes("around SE offset", baseAddr + seInnerOffset - 8, 32);

            auto callAddr = FindNearestCall(baseAddr, seInnerOffset);
            if (!callAddr) {
                logger::error("Tweaks: SneakJumpHeight SKIPPED — could not find CALL instruction");
                DumpBytes("search area", baseAddr + seInnerOffset - 32, 80);
                return;
            }

            auto& trampoline = SKSE::GetTrampoline();
            OriginalGetScale = trampoline.write_call<5>(callAddr, GetScale);

            logger::info("Tweaks: SneakJumpHeight INSTALLED at {:X} (offset +0x{:X})",
                callAddr, callAddr - baseAddr);
        }
    }

    void Install()
    {
        AbsorptionChance::Install();
        ConcentrationCasting::Install();
        JumpHeight::Install();
    }
}
