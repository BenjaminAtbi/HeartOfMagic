#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

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
    inline constexpr std::uint32_t kMessageApiVersion = 2;  // the first SCSSoulCharge API that sends 'SCST'

    // The original spell's FormID when a_formID is a staff copy, else a_formID unchanged.
    // Only runtime forms (index 0xFF) are asked about, so plugin spells cost one shift.
    // Not noexcept: a throw from the lookup must reach Resolve's catch, not std::terminate.
    inline std::uint32_t ResolveWith(GetOriginalSpellFn a_getOriginal, std::uint32_t a_formID)
    {
        if (!a_getOriginal || (a_formID >> 24) != kRuntimeFormIndex) {
            return a_formID;
        }
        const std::uint32_t original = a_getOriginal(a_formID);
        return original != 0 ? original : a_formID;
    }

    // SCSSoulCharge broadcasts one SKSE message per staff cast (sender "SCSSoulCharge", its src/api.h).
    // A staff (enchantment) cast may send no TESSpellCastEvent: Dynamic Magic Modification Framework 2.0.3
    // drops it (seen in game 2026-11-18, probe build: hand casts reported, staff casts never), so the message
    // is the one report HoM counts. Layout copied here, not included.
    inline constexpr const char* kStaffSender = "SCSSoulCharge";
    inline constexpr std::uint32_t kStaffCastMessageType = 'SCST';
    struct StaffCast
    {
        std::uint32_t copy{ 0 };           // the runtime enchantment the staff cast (0xFF FormID)
        std::uint32_t spell{ 0 };          // the spell it was made from
        std::uint32_t concentration{ 0 };  // 1 for a concentration spell (sent once, when it starts streaming)
    };
    static_assert(sizeof(StaffCast) == 12, "SCSSoulCharge's StaffCastMessage is 12 bytes");

    // The cast a message carries, or nothing for another message type, fewer than 12 bytes or no spell
    inline std::optional<StaffCast> DecodeStaffCast(std::uint32_t a_type, std::uint32_t a_dataLen, const void* a_data) noexcept
    {
        // later builds append fields after these 12 bytes only (SCSSoulCharge's contract): the first 12 are read
        if (a_type != kStaffCastMessageType || a_dataLen < sizeof(StaffCast) || !a_data) return std::nullopt;
        StaffCast cast;
        std::memcpy(&cast, a_data, sizeof(cast));
        if (cast.spell == 0) return std::nullopt;
        return cast;
    }

    // SCSSoulCharge (API 2) appends a marker (" \u2605" by default, its INI) to the name of the spell bound to the
    // staff in the right hand while it is held, re-adding it every frame. HoM's own names never keep it:
    // a_name without a trailing a_marker; unchanged for an empty marker or a name that does not end with it.
    inline std::string StripMarkerWith(std::string_view a_marker, std::string_view a_name)
    {
        if (a_marker.empty() || a_name.size() < a_marker.size() || !a_name.ends_with(a_marker)) return std::string(a_name);
        return std::string(a_name.substr(0, a_name.size() - a_marker.size()));
    }

    // The marker is on one spell only, the one SCSSoulCharge_GetHeldStaffSpell names (0 = none): a_name of any
    // other spell stays as it is, whatever it ends with.
    inline std::string StripHeldMarkerWith(std::uint32_t a_heldSpell, std::uint32_t a_spell, std::string_view a_marker,
        std::string_view a_name)
    {
        if (a_heldSpell == 0 || a_spell != a_heldSpell) return std::string(a_name);
        return StripMarkerWith(a_marker, a_name);
    }

    // Looks SCSSoulCharge.dll up once every plugin has loaded (kPostPostLoad) and logs
    // whether the link is on. Without the DLL, or with an older API, it stays off.
    void Init();
    // At kPostLoad: listen for SCSSoulCharge's staff-cast messages (does nothing without the DLL)
    void RegisterCastListener();
    // True once the listener is registered: staff casts are then counted from the message only
    bool IsListening() noexcept;
    // a_name (spell a_spell's) without SCSSoulCharge's held-staff marker: StripHeldMarkerWith with the DLL's held
    // spell and marker, both asked on each call (the marker follows its INI). Unchanged without the DLL or with
    // API < 2. Any thread.
    std::string StripNameMarker(std::uint32_t a_spell, std::string_view a_name);

    // ResolveWith using the DLL's lookup; a_formID unchanged while the link is off. Any thread.
    // A throw from the DLL turns the link off (logged once) instead of ending the game.
    std::uint32_t Resolve(std::uint32_t a_formID) noexcept;
}
