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

    bool IsDebuggingEnabled()
    {
        return Settings::Debugging.load(std::memory_order_relaxed);
    }

    void DumpBytes(const char* a_label, std::uintptr_t a_addr, std::size_t a_count)
    {
        if (!IsDebuggingEnabled()) {
            return;
        }

        std::string hex;
        for (std::size_t i = 0; i < a_count; ++i) {
            auto byte = *reinterpret_cast<std::uint8_t*>(a_addr + i);
            hex += std::format("{:02X} ", byte);
        }

        logger::info("  [DUMP] {} @ {:X}: {}", a_label, a_addr, hex);
    }

    bool IsCallInstruction(std::uintptr_t a_addr)
    {
        return *reinterpret_cast<std::uint8_t*>(a_addr) == 0xE8;
    }

    std::uintptr_t FindNearestCall(
        std::uintptr_t a_baseAddr,
        std::ptrdiff_t a_expectedOffset,
        std::ptrdiff_t a_searchRadius = 32)
    {
        const auto expected = a_baseAddr + a_expectedOffset;

        if (IsCallInstruction(expected)) {
            if (IsDebuggingEnabled()) {
                logger::info("  [SCAN] Found E8 at expected offset +0x{:X}", a_expectedOffset);
            }
            return expected;
        }

        for (std::ptrdiff_t delta = 1; delta <= a_searchRadius; ++delta) {
            if (IsCallInstruction(expected + delta)) {
                logger::warn("  [SCAN] E8 not at +0x{:X}, found at +0x{:X} (delta +{})",
                    a_expectedOffset, a_expectedOffset + delta, delta);
                return expected + delta;
            }

            if (IsCallInstruction(expected - delta)) {
                logger::warn("  [SCAN] E8 not at +0x{:X}, found at +0x{:X} (delta -{})",
                    a_expectedOffset, a_expectedOffset - delta, delta);
                return expected - delta;
            }
        }

        logger::error("  [SCAN] No E8 found within +/-{} bytes of +0x{:X}",
            a_searchRadius, a_expectedOffset);
        return 0;
    }

    std::uint32_t NextTraceHit(std::atomic_uint32_t& a_counter)
    {
        if (!IsDebuggingEnabled()) {
            return 0;
        }

        const auto hit = a_counter.fetch_add(1, std::memory_order_relaxed) + 1;
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
        if (!IsDebuggingEnabled()) {
            return;
        }

        logger::info("  {} runtime trace active: first {} hits and every {}th hit thereafter",
            a_hookName, kInitialRuntimeTraceCount, kPeriodicRuntimeTraceCount);
    }
}

namespace Fixes
{
    namespace NordRaceStats
    {
        void Install()
        {
            if (!Settings::NordRaceStats.load(std::memory_order_relaxed)) {
                logger::info("Fixes: NordRaceStats disabled in settings");
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                logger::error("Fixes: NordRaceStats failed - PlayerCharacter singleton is null");
                return;
            }

            player->InitItemImpl();
            logger::info("Fixes: NordRaceStats applied (recalculated player stats)");
        }
    }

    namespace ScaleMovementSpeed
    {
        std::atomic_uint32_t RuntimeTraceHits{ 0 };

        float Call(RE::TESObjectREFR* a_ref)
        {
            const float scale = Callback(a_ref);
            if (a_ref != RE::PlayerCharacter::GetSingleton()) {
                return scale;
            }

            const auto enabled = Settings::ScaleMovementSpeed.load(std::memory_order_relaxed);
            if (auto hit = NextTraceHit(RuntimeTraceHits)) {
                if (enabled) {
                    logger::info("Fixes: ScaleMovementSpeed call #{} - {} scale {:.4f} -> 1.0000",
                        hit, DescribeForm(a_ref), scale);
                } else {
                    logger::info("Fixes: ScaleMovementSpeed call #{} - disabled, {} scale {:.4f}",
                        hit, DescribeForm(a_ref), scale);
                }
            }

            return enabled ? 1.0f : scale;
        }

        void Install()
        {
            constexpr std::uintptr_t funcOffset = 0x617B00;
            constexpr std::ptrdiff_t seInnerOffset = 0x1A;

            const auto baseAddr = REL::Offset(funcOffset).address();
            if (IsDebuggingEnabled()) {
                logger::info("Fixes: ScaleMovementSpeed base addr: {:X}", baseAddr);
                DumpBytes("func prologue", baseAddr, 48);
                DumpBytes("around SE offset", baseAddr + seInnerOffset - 8, 32);
            }

            const auto callAddr = FindNearestCall(baseAddr, seInnerOffset);
            if (!callAddr) {
                logger::error("Fixes: ScaleMovementSpeed skipped - could not find CALL instruction");
                DumpBytes("search area", baseAddr, 64);
                return;
            }

            auto& trampoline = SKSE::GetTrampoline();
            Callback = trampoline.write_call<5>(callAddr, Call);

            logger::info("Fixes: ScaleMovementSpeed INSTALLED at {:X} (offset +0x{:X})",
                callAddr, callAddr - baseAddr);
            if (IsDebuggingEnabled()) {
                logger::info("  setting at load: {}", Settings::ScaleMovementSpeed.load(std::memory_order_relaxed));
            }
            LogRuntimeTracePolicy("ScaleMovementSpeed");
        }
    }

    void Install()
    {
        ScaleMovementSpeed::Install();
    }
}

namespace Tweaks
{
    namespace AbsorptionChance
    {
        std::atomic_uint32_t RuntimeTraceHits{ 0 };

        std::uint32_t Call(RE::ActorValueOwner* a_owner, RE::ActorValue a_akValue)
        {
            if (!a_owner) {
                logger::error("Tweaks: AbsorptionChance called with a null ActorValueOwner");
                return 0;
            }

            const auto rawValue = a_owner->GetActorValue(a_akValue);
            const auto enabled = Settings::AbsorptionChance.load(std::memory_order_relaxed);
            const auto configuredCap = Settings::AbsorptionChanceCap.load(std::memory_order_relaxed);
            const auto result = enabled ?
                static_cast<std::uint32_t>((std::min)(rawValue, configuredCap)) :
                static_cast<std::uint32_t>(rawValue);

            if (auto hit = NextTraceHit(RuntimeTraceHits)) {
                if (enabled) {
                    logger::info("Tweaks: AbsorptionChance call #{} - AV {} ({}) raw {:.2f}, cap {:.2f}, result {}",
                        hit,
                        GetActorValueName(a_akValue),
                        static_cast<std::uint32_t>(a_akValue),
                        rawValue,
                        configuredCap,
                        result);
                } else {
                    logger::info("Tweaks: AbsorptionChance call #{} - disabled, AV {} ({}) raw {:.2f}, result {}",
                        hit,
                        GetActorValueName(a_akValue),
                        static_cast<std::uint32_t>(a_akValue),
                        rawValue,
                        result);
                }
            }

            return result;
        }

        void Install()
        {
            constexpr std::uintptr_t funcOffset = 0x639720;
            constexpr std::ptrdiff_t seInnerOffset = 0x53;

            const auto baseAddr = REL::Offset(funcOffset).address();
            if (IsDebuggingEnabled()) {
                logger::info("Tweaks: AbsorptionChance base addr: {:X}", baseAddr);
                DumpBytes("func prologue", baseAddr, 48);
                DumpBytes("around SE offset", baseAddr + seInnerOffset - 8, 32);
            }

            const auto patchSite = baseAddr + seInnerOffset;
            bool foundPattern = false;
            std::ptrdiff_t patternDelta = 0;

            for (std::ptrdiff_t scan = -32; scan <= 32; ++scan) {
                const auto scanAddr = patchSite + scan;
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
                    if (IsDebuggingEnabled()) {
                        logger::info("  [SCAN] Found FF 50 08 + F3 0F 2C C0 pattern at delta {} from expected",
                            patternDelta);
                    }
                    break;
                }
            }

            if (!foundPattern) {
                logger::error("Tweaks: AbsorptionChance skipped - cannot find call+convert pattern near +0x{:X}",
                    seInnerOffset);
                DumpBytes("wide scan area", patchSite - 32, 96);
                return;
            }

            const auto adjustedSite = patchSite + patternDelta;
            if (IsDebuggingEnabled()) {
                logger::info("  Adjusted patch site: {:X} (delta {})", adjustedSite, patternDelta);
                DumpBytes("adjusted patch site", adjustedSite, 32);
            }

            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(adjustedSite, Call);
            REL::safe_fill(adjustedSite + 5, REL::NOP, 3);

            logger::info("Tweaks: AbsorptionChance INSTALLED at {:X} (adjusted offset +0x{:X})",
                adjustedSite, adjustedSite - baseAddr);
            if (IsDebuggingEnabled()) {
                logger::info("  Patch layout: 5 call + 3 NOP = 8 bytes");
                logger::info("  setting at load: {}, cap at load: {}",
                    Settings::AbsorptionChance.load(std::memory_order_relaxed),
                    Settings::AbsorptionChanceCap.load(std::memory_order_relaxed));
            }
            LogRuntimeTracePolicy("AbsorptionChance");
        }
    }

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

            const auto available = a_usePermanent ?
                a_avOwner->GetPermanentActorValue(a_akValue) :
                a_avOwner->GetActorValue(a_akValue);

            const auto spellType = a_spell ? a_spell->GetSpellType() : RE::MagicSystem::SpellType::kSpell;
            const auto enabled = Settings::ConcentrationCasting.load(std::memory_order_relaxed);

            float baseValue = 0.0f;
            bool allowed = available >= a_cost;
            const char* rule = "vanilla";

            if (enabled && spellType == RE::MagicSystem::SpellType::kSpell) {
                baseValue = a_avOwner->GetBaseActorValue(a_akValue);
                allowed = (baseValue >= a_cost) && (available >= a_cost);
                rule = "base+available";
            }

            if (auto hit = NextTraceHit(RuntimeTraceHits)) {
                const auto baseValueText = spellType == RE::MagicSystem::SpellType::kSpell ?
                    std::format("{:.2f}", baseValue) :
                    "n/a";
                logger::info(
                    "Tweaks: ConcentrationCasting call #{} - spell {} type {} cost {:.2f}, available {:.2f}, base {}, permanent {}, rule {}, allowed {}",
                    hit,
                    DescribeForm(a_spell),
                    GetSpellTypeName(spellType),
                    a_cost,
                    available,
                    baseValueText,
                    a_usePermanent,
                    rule,
                    allowed);
            }

            if (!a_spell) {
                logger::warn("Tweaks: ConcentrationCasting received a null MagicItem");
            }

            return allowed;
        }

        void Install()
        {
            constexpr std::uintptr_t funcOffset = 0x5465A0;
            constexpr std::ptrdiff_t seInnerOffset = 0x1B3;

            const auto baseAddr = REL::Offset(funcOffset).address();
            if (IsDebuggingEnabled()) {
                logger::info("Tweaks: ConcentrationCasting base addr: {:X}", baseAddr);
                DumpBytes("func prologue", baseAddr, 48);
            }

            const auto patchSite = baseAddr + seInnerOffset;
            DumpBytes("SE patch site", patchSite - 8, 96);

            bool foundPattern = false;
            std::ptrdiff_t patternDelta = 0;

            for (std::ptrdiff_t scan = -64; scan <= 64; ++scan) {
                const auto scanAddr = patchSite + 0x1E + scan;
                if (*reinterpret_cast<std::uint8_t*>(scanAddr) == 0xFF &&
                    *reinterpret_cast<std::uint8_t*>(scanAddr + 1) == 0x90 &&
                    *reinterpret_cast<std::uint32_t*>(scanAddr + 2) == 0x000002A8) {
                    foundPattern = true;
                    patternDelta = scan;
                    if (IsDebuggingEnabled()) {
                        logger::info("  [SCAN] Found FF 90 A8 02 00 00 pattern at delta {} from expected",
                            patternDelta);
                    }
                    break;
                }
            }

            if (!foundPattern) {
                logger::error("Tweaks: ConcentrationCasting skipped - cannot find call [rax+0x2A8] pattern near +0x{:X}",
                    seInnerOffset);
                DumpBytes("wide scan area", patchSite - 32, 128);
                return;
            }

            const auto adjustedSite = patchSite + patternDelta;
            if (IsDebuggingEnabled()) {
                logger::info("  Adjusted patch site: {:X} (delta {})", adjustedSite, patternDelta);
                DumpBytes("adjusted patch site", adjustedSite, 64);
            }

            if (*reinterpret_cast<std::uint8_t*>(adjustedSite) != 0x80 ||
                *reinterpret_cast<std::uint8_t*>(adjustedSite + 1) != 0xBC ||
                *reinterpret_cast<std::uint8_t*>(adjustedSite + 2) != 0x24) {
                logger::error("Tweaks: ConcentrationCasting skipped - unexpected instruction at patch site");
                return;
            }

            constexpr std::size_t kPatchRegionSize = 0x38;

            std::uint8_t setup[] = {
                0x49, 0x89, 0xF8,
                0x0F, 0x28, 0xDF,
                0x8A, 0x84, 0x24, 0xD8, 0x00, 0x00, 0x00,
                0x88, 0x44, 0x24, 0x20,
            };
            constexpr std::size_t kSetupSize = sizeof(setup);
            constexpr std::size_t kCallSize = 5;
            constexpr std::size_t kNopSize = kPatchRegionSize - kSetupSize - kCallSize;

            REL::safe_write(adjustedSite, setup, kSetupSize);

            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(adjustedSite + kSetupSize, Call);
            REL::safe_fill(adjustedSite + kSetupSize + kCallSize, REL::NOP, kNopSize);

            logger::info("Tweaks: ConcentrationCasting INSTALLED at {:X} (adjusted offset +0x{:X})",
                adjustedSite, adjustedSite - baseAddr);
            if (IsDebuggingEnabled()) {
                logger::info("  Patch layout: {} setup + {} call + {} NOP = {} bytes",
                    kSetupSize, kCallSize, kNopSize, kPatchRegionSize);
                logger::info("  setting at load: {}", Settings::ConcentrationCasting.load(std::memory_order_relaxed));
            }
            LogRuntimeTracePolicy("ConcentrationCasting");
        }
    }

    namespace JumpHeight
    {
        std::atomic_uint32_t RuntimeTraceHits{ 0 };

        float GetScale(RE::TESObjectREFR* a_ref)
        {
            const float scale = OriginalGetScale(a_ref);

            auto* actor = a_ref->As<RE::Actor>();
            if (!actor || actor != RE::PlayerCharacter::GetSingleton() || !actor->IsSneaking()) {
                return scale;
            }

            const auto enabled = Settings::SneakJumpHeight.load(std::memory_order_relaxed);
            const auto modifier = Settings::SneakJumpHeightMod.load(std::memory_order_relaxed);
            const auto adjustedScale = enabled ? scale * modifier : scale;

            if (auto hit = NextTraceHit(RuntimeTraceHits)) {
                if (enabled) {
                    logger::info("Tweaks: SneakJumpHeight call #{} - {} scale {:.4f} * {:.2f} = {:.4f}",
                        hit, DescribeForm(actor), scale, modifier, adjustedScale);
                } else {
                    logger::info("Tweaks: SneakJumpHeight call #{} - disabled, {} scale {:.4f}",
                        hit, DescribeForm(actor), scale);
                }
            }

            return adjustedScale;
        }

        void Install()
        {
            constexpr std::uintptr_t funcOffset = 0x5DA540;
            constexpr std::ptrdiff_t seInnerOffset = 0x190;

            const auto baseAddr = REL::Offset(funcOffset).address();
            if (IsDebuggingEnabled()) {
                logger::info("Tweaks: SneakJumpHeight base addr: {:X}", baseAddr);
                DumpBytes("func prologue", baseAddr, 48);
                DumpBytes("around SE offset", baseAddr + seInnerOffset - 8, 32);
            }

            const auto callAddr = FindNearestCall(baseAddr, seInnerOffset);
            if (!callAddr) {
                logger::error("Tweaks: SneakJumpHeight skipped - could not find CALL instruction");
                DumpBytes("search area", baseAddr + seInnerOffset - 32, 80);
                return;
            }

            auto& trampoline = SKSE::GetTrampoline();
            OriginalGetScale = trampoline.write_call<5>(callAddr, GetScale);

            logger::info("Tweaks: SneakJumpHeight INSTALLED at {:X} (offset +0x{:X})",
                callAddr, callAddr - baseAddr);
            if (IsDebuggingEnabled()) {
                logger::info("  setting at load: {}, modifier at load: {}",
                    Settings::SneakJumpHeight.load(std::memory_order_relaxed),
                    Settings::SneakJumpHeightMod.load(std::memory_order_relaxed));
            }
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
