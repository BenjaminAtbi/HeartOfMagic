#pragma once

#include <cstdint>

// =============================================================================
// StaffSpellBridge - staff casts from SCSSoulCharge count as their original spell
// =============================================================================
// The standalone staff mod SCSSoulCharge fires a runtime copy of the spell bound to
// a staff: an EnchantmentItem (spellType kStaffEnchantment) in the 0xFF (runtime)
// form range. HoM keys learning XP and early-learned effectiveness by the spell's
// FormID, so without this a staff cast gave no XP (SpellCastHandler wants a
// SpellItem of type kSpell) and a concentration staff effect was never weakened.
// SCSSoulCharge.dll exports a lookup from the copy to the original SpellItem; this
// asks it, and only for runtime forms. Without the DLL nothing changes.
//
// Contract (SCSSoulCharge, API version 1, safe from any thread):
//   std::uint32_t SCSSoulCharge_ApiVersion()
//   std::uint32_t SCSSoulCharge_GetOriginalSpell(std::uint32_t formID)  // 0 = not a staff copy

namespace StaffSpellBridge
{
    using GetOriginalSpellFn = std::uint32_t (*)(std::uint32_t);

    inline constexpr std::uint32_t kRuntimeFormIndex = 0xFF;
    inline constexpr std::uint32_t kMinApiVersion = 1;

    // The original spell's FormID when a_formID is a staff copy, else a_formID unchanged.
    // Only runtime forms (index 0xFF) are asked about, so plugin spells cost one shift.
    inline std::uint32_t ResolveWith(GetOriginalSpellFn a_getOriginal, std::uint32_t a_formID) noexcept
    {
        if (!a_getOriginal || (a_formID >> 24) != kRuntimeFormIndex) {
            return a_formID;
        }
        const std::uint32_t original = a_getOriginal(a_formID);
        return original != 0 ? original : a_formID;
    }

    // Looks SCSSoulCharge.dll up once every plugin has loaded (kPostPostLoad) and logs
    // whether the link is on. Without the DLL, or with an older API, it stays off.
    void Init();

    // ResolveWith using the DLL's lookup; a_formID unchanged while the link is off. Any thread.
    std::uint32_t Resolve(std::uint32_t a_formID) noexcept;
}
