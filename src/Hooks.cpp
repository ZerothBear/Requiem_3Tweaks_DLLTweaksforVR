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
    inline constexpr std::uint32_t kInitialRuntimeTraceCount = 5;
    inline constexpr std::uint32_t kPeriodicRuntimeTraceCount = 100;

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

    std::uint32_t NextTraceHit(std::atomic_uint32_t& a_counter)
    {
        auto hit = a_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit <= kInitialRuntimeTraceCount || (hit % kPeriodicRuntimeTraceCount) == 0) {
            return hit;
        }

        return 0;
    }

    const char* GetActorValueName(RE::ActorValue a_value)
    {
        auto* name = RE::ActorValueList::GetActorValueName(a_value);
        return name ? name : "<unknown>";
    }

    const char* GetSpellTypeName(RE::MagicSystem::SpellType a_spellType)
    {
        switch (a_spellType) {
        case RE::MagicSystem::SpellType::kSpell:
            return "Spell";
        case RE::MagicSystem::SpellType::kDisease:
            return "Disease";
        case RE::MagicSystem::SpellType::kPower:
            return "Power";
        case RE::MagicSystem::SpellType::kLesserPower:
            return "LesserPower";
        case RE::MagicSystem::SpellType::kAbility:
            return "Ability";
        case RE::MagicSystem::SpellType::kPoison:
            return "Poison";
        case RE::MagicSystem::SpellType::kEnchantment:
            return "Enchantment";
        case RE::MagicSystem::SpellType::kPotion:
            return "Potion";
        case RE::MagicSystem::SpellType::kWortCraft:
            return "WortCraft";
        case RE::MagicSystem::SpellType::kLeveledSpell:
            return "LeveledSpell";
        case RE::MagicSystem::SpellType::kAddiction:
            return "Addiction";
        case RE::MagicSystem::SpellType::kVoicePower:
            return "VoicePower";
        case RE::MagicSystem::SpellType::kStaffEnchantment:
            return "StaffEnchantment";
        case RE::MagicSystem::SpellType::kScroll:
            return "Scroll";
        default:
            return "Unknown";
        }
    }

    std::string DescribeForm(const RE::TESForm* a_form)
    {
        if (!a_form) {
            return "<null>";
        }

        auto* name = a_form->GetName();
        if (name && name[0] != '\0') {
            return std::format("{} [form {:08X}]", name, a_form->GetFormID());
        }

        return std::format("<unnamed> [form {:08X}]", a_form->GetFormID());
    }

    void LogRuntimeTracePolicy(const char* a_hookName)
    {
        logger::info("  {} runtime trace active: first {} hits and every {}th hit thereafter",
            a_hookName, kInitialRuntimeTraceCount, kPeriodicRuntimeTraceCount);
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
        std::atomic_uint32_t RuntimeTraceHits{ 0 };

        float Call(RE::TESObjectREFR* a_ref)
        {
            float scale = Callback(a_ref);

            if (a_ref == RE::PlayerCharacter::GetSingleton()) {
                if (auto hit = NextTraceHit(RuntimeTraceHits)) {
                    logger::info("Fixes: ScaleMovementSpeed call #{} — {} scale {:.4f} -> 1.0000",
                        hit, DescribeForm(a_ref), scale);
                }
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

            auto fixedCallAddr = FindNearestCall(baseAddr, seInnerOffset);
            if (!fixedCallAddr) {
                logger::error("Fixes: ScaleMovementSpeed SKIPPED â€” could not find CALL instruction");
                DumpBytes("search area", baseAddr, 64);
                return;
            }

            auto& fixedTrampoline = SKSE::GetTrampoline();
            Callback = fixedTrampoline.write_call<5>(fixedCallAddr, Call);

            logger::info("Fixes: ScaleMovementSpeed INSTALLED at {:X} (offset +0x{:X})",
                fixedCallAddr, fixedCallAddr - baseAddr);
            LogRuntimeTracePolicy("ScaleMovementSpeed");
            return;

#if 0
            auto callAddr = FindNearestCall(baseAddr, seInnerOffset);
            if (!foundPattern) {
                logger::error("Tweaks: AbsorptionChance SKIPPED â€” "
                    "cannot find call+convert pattern near +0x{:X}", seInnerOffset);
                DumpBytes("wide scan area", patchSite - 32, 96);
                return;
            }

            auto adjustedSite = patchSite + patternDelta;
            logger::info("  Adjusted patch site: {:X} (delta {})", adjustedSite, patternDelta);
            DumpBytes("adjusted patch site", adjustedSite, 32);

            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(adjustedSite, Call);

            // Overwrite the remainder of cvttss2si eax, xmm0.
            REL::safe_fill(adjustedSite + 5, REL::NOP, 3);

            logger::info("Tweaks: AbsorptionChance INSTALLED at {:X} (adjusted offset +0x{:X})",
                adjustedSite, adjustedSite - baseAddr);
            logger::info("  Patch layout: 5 call + 3 NOP = 8 bytes");
            LogRuntimeTracePolicy("AbsorptionChance");
            return;

#endif
#if 0
            if (!callAddr) {
                logger::error("Fixes: ScaleMovementSpeed SKIPPED — could not find CALL instruction");
                DumpBytes("search area", baseAddr, 64);
                return;
            }

            auto& trampoline = SKSE::GetTrampoline();
            Callback = trampoline.write_call<5>(callAddr, Call);

            logger::info("Fixes: ScaleMovementSpeed INSTALLED at {:X} (offset +0x{:X})",
                callAddr, callAddr - baseAddr);
            LogRuntimeTracePolicy("ScaleMovementSpeed");
#endif
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
        std::atomic_uint32_t RuntimeTraceHits{ 0 };

        std::uint32_t Call(RE::ActorValueOwner* a_owner, RE::ActorValue a_akValue)
        {
            if (!a_owner) {
                logger::error("Tweaks: AbsorptionChance called with a null ActorValueOwner");
                return 0;
            }

            float value = a_owner->GetActorValue(a_akValue);

            auto* gmst = RE::GameSettingCollection::GetSingleton();
            auto* setting = gmst ? gmst->GetSetting("fPlayerMaxResistance") : nullptr;
            float cap = setting ? setting->GetFloat() : 85.0f;
            auto clamped = static_cast<std::uint32_t>((std::min)(value, cap));

            if (auto hit = NextTraceHit(RuntimeTraceHits)) {
                logger::info("Tweaks: AbsorptionChance call #{} — AV {} ({}) raw {:.2f}, cap {:.2f}, result {}",
                    hit,
                    GetActorValueName(a_akValue),
                    static_cast<std::uint32_t>(a_akValue),
                    value,
                    cap,
                    clamped);
            }

            return clamped;
        }

        void InstallImpl()
        {
            constexpr std::uintptr_t funcOffset = 0x639720;
            constexpr std::ptrdiff_t seInnerOffset = 0x53;

            auto baseAddr = REL::Offset(funcOffset).address();
            logger::info("Tweaks: AbsorptionChance â€” base addr: {:X}", baseAddr);
            DumpBytes("func prologue", baseAddr, 48);
            DumpBytes("around SE offset", baseAddr + seInnerOffset - 8, 32);

            // In VR the SE direct call site became:
            //   mov r8, [rdi+0x18]
            //   call qword ptr [r8+0x08]
            //   cvttss2si eax, xmm0
            //
            // This tweak replaces the entire 8-byte call+convert block with:
            //   call Call
            //   nop nop nop
            auto patchSite = baseAddr + seInnerOffset;
            bool foundPattern = false;
            std::ptrdiff_t patternDelta = 0;

            for (std::ptrdiff_t scan = -32; scan <= 32; ++scan) {
                auto scanAddr = patchSite + scan;
                auto* bytes = reinterpret_cast<std::uint8_t*>(scanAddr);

                if (bytes[0] == 0x41 &&
                    bytes[1] == 0xFF &&
                    bytes[2] == 0x50 &&
                    bytes[3] == 0x08 &&
                    bytes[4] == 0xF3 &&
                    bytes[5] == 0x0F &&
                    bytes[6] == 0x2C &&
                    bytes[7] == 0xC0) {
                    foundPattern = true;
                    patternDelta = scan;
                    logger::info(
                        "  [SCAN] Found FF 50 08 + F3 0F 2C C0 pattern at delta {} from expected",
                        patternDelta);
                    break;
                }
            }

            if (!foundPattern) {
                logger::error("Tweaks: AbsorptionChance SKIPPED â€” "
                    "cannot find call+convert pattern near +0x{:X}", seInnerOffset);
                DumpBytes("wide scan area", patchSite - 32, 96);
                return;
            }

            auto adjustedSite = patchSite + patternDelta;
            logger::info("  Adjusted patch site: {:X} (delta {})", adjustedSite, patternDelta);
            DumpBytes("adjusted patch site", adjustedSite, 32);

            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(adjustedSite, Call);

            // Overwrite the remainder of cvttss2si eax, xmm0.
            REL::safe_fill(adjustedSite + 5, REL::NOP, 3);

            logger::info("Tweaks: AbsorptionChance INSTALLED at {:X} (adjusted offset +0x{:X})",
                adjustedSite, adjustedSite - baseAddr);
            logger::info("  Patch layout: 5 call + 3 NOP = 8 bytes");
            LogRuntimeTracePolicy("AbsorptionChance");
        }

        void Install()
        {
            if (!Settings::AbsorptionChance) {
                logger::info("Tweaks: AbsorptionChance DISABLED by INI");
                return;
            }

            InstallImpl();
            return;

#if 0
            constexpr std::uintptr_t funcOffset = 0x639720;
            constexpr std::ptrdiff_t seInnerOffset = 0x53;

            auto baseAddr = REL::Offset(funcOffset).address();
            logger::info("Tweaks: AbsorptionChance — base addr: {:X}", baseAddr);
            DumpBytes("func prologue", baseAddr, 48);
            DumpBytes("around SE offset", baseAddr + seInnerOffset - 8, 32);

            auto patchSite = baseAddr + seInnerOffset;
            std::uintptr_t callAddr = 0;

            // In VR the SE direct call site became:
            //   mov r8, [rdi+0x18]
            //   call qword ptr [r8+0x08]
            //   cvttss2si eax, xmm0
            //
            // This tweak replaces the entire 8-byte call+convert block with:
            //   call Call
            //   nop nop nop
            bool foundPattern = false;
            std::ptrdiff_t patternDelta = 0;

            for (std::ptrdiff_t scan = -32; scan <= 32; ++scan) {
                auto scanAddr = patchSite + scan;
                auto* bytes = reinterpret_cast<std::uint8_t*>(scanAddr);

                if (bytes[0] == 0x41 &&
                    bytes[1] == 0xFF &&
                    bytes[2] == 0x50 &&
                    bytes[3] == 0x08 &&
                    bytes[4] == 0xF3 &&
                    bytes[5] == 0x0F &&
                    bytes[6] == 0x2C &&
                    bytes[7] == 0xC0) {
                    foundPattern = true;
                    patternDelta = scan;
                    logger::info(
                        "  [SCAN] Found FF 50 08 + F3 0F 2C C0 pattern at delta {} from expected",
                        patternDelta);
                    break;
                }
            }
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
            LogRuntimeTracePolicy("AbsorptionChance");
#endif
        }
    }

    // -------------------------------------------------------------------------
    // ConcentrationCasting: Prevents casting concentration spells when magicka
    // is insufficient.
    // -------------------------------------------------------------------------
    namespace ConcentrationCasting
    {
        std::atomic_uint32_t RuntimeTraceHits{ 0 };

        bool Call(RE::ActorValueOwner* a_avOwner, RE::ActorValue a_akValue,
                  RE::MagicItem* a_spell, float a_cost, bool a_usePermanent)
        {
            if (!a_avOwner) {
                logger::error("Tweaks: ConcentrationCasting called with a null ActorValueOwner");
                return false;
            }

            float available;
            if (a_usePermanent) {
                available = a_avOwner->GetPermanentActorValue(a_akValue);
            } else {
                available = a_avOwner->GetActorValue(a_akValue);
            }

            auto spellType = a_spell ? a_spell->GetSpellType() : RE::MagicSystem::SpellType::kSpell;
            float baseValue = 0.0f;
            bool allowed;

            if (spellType == RE::MagicSystem::SpellType::kSpell) {
                baseValue = a_avOwner->GetBaseActorValue(a_akValue);
                allowed = (baseValue >= a_cost) && (available >= a_cost);
            } else {
                allowed = available >= a_cost;
            }

            if (auto hit = NextTraceHit(RuntimeTraceHits)) {
                auto baseValueText = spellType == RE::MagicSystem::SpellType::kSpell ?
                    std::format("{:.2f}", baseValue) :
                    "n/a";
                logger::info(
                    "Tweaks: ConcentrationCasting call #{} — spell {} type {} cost {:.2f}, available {:.2f}, base {}, permanent {}, allowed {}",
                    hit,
                    DescribeForm(a_spell),
                    GetSpellTypeName(spellType),
                    a_cost,
                    available,
                    baseValueText,
                    a_usePermanent,
                    allowed);
            }

            if (!a_spell) {
                logger::warn("Tweaks: ConcentrationCasting received a null MagicItem");
                return allowed;
            }

            if (a_spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
                return allowed;
            }

            return allowed;
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
            LogRuntimeTracePolicy("ConcentrationCasting");
        }
    }

    // -------------------------------------------------------------------------
    // JumpHeight: Reduces jump height while sneaking by a configurable modifier.
    // -------------------------------------------------------------------------
    namespace JumpHeight
    {
        std::atomic_uint32_t RuntimeTraceHits{ 0 };

        float GetScale(RE::TESObjectREFR* a_ref)
        {
            float scale = OriginalGetScale(a_ref);

            auto* actor = a_ref->As<RE::Actor>();
            if (actor && actor == RE::PlayerCharacter::GetSingleton() && actor->IsSneaking()) {
                float adjustedScale = scale * Settings::SneakJumpHeightMod;
                if (auto hit = NextTraceHit(RuntimeTraceHits)) {
                    logger::info("Tweaks: SneakJumpHeight call #{} — {} scale {:.4f} * {:.2f} = {:.4f}",
                        hit, DescribeForm(actor), scale, Settings::SneakJumpHeightMod, adjustedScale);
                }
                return adjustedScale;
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
            LogRuntimeTracePolicy("SneakJumpHeight");
        }
    }

    void Install()
    {
        AbsorptionChance::Install();
        ConcentrationCasting::Install();
        JumpHeight::Install();
    }
}
