// staff-bridge-check - offline check of StaffSpellBridge::ResolveWith
// (plugins/spelllearning/include/StaffSpellBridge.h, header part only).
//
//   staff-bridge-check --self-test
//
// Without SCSSoulCharge.dll (no lookup) every FormID comes back unchanged; a plugin
// spell (index below 0xFF) is never asked about; a runtime form the DLL does not know
// (lookup returns 0) stays itself; a staff copy comes back as its original spell.
// Init's GetModuleHandleW/GetProcAddress half needs the game and the DLL: the log
// line "StaffSpellBridge: linked" or "is not loaded" shows it.
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "StaffSpellBridge.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    constexpr std::uint32_t kStaffCopy = 0xFF000801;     // a runtime copy the fake DLL knows
    constexpr std::uint32_t kOtherRuntime = 0xFF000900;  // a runtime form that is no staff copy
    constexpr std::uint32_t kOriginal = 0x0001C789;      // Skyrim.esm Flames
    constexpr std::uint32_t kPluginSpell = 0x0A012345;   // a spell from a load-order plugin
    constexpr std::uint32_t kLightSpell = 0xFE001800;    // a spell from a light plugin (ESL)

    int g_failures = 0;
    int g_checks = 0;
    int g_lookups = 0;

    void Check(bool ok, const char* what)
    {
        ++g_checks;
        if (!ok) {
            ++g_failures;
            std::printf("FAIL %s\n", what);
        }
    }

    std::uint32_t FakeGetOriginal(std::uint32_t a_formID)
    {
        ++g_lookups;
        return a_formID == kStaffCopy ? kOriginal : 0;
    }

    std::uint32_t ThrowingGetOriginal(std::uint32_t)
    {
        throw std::runtime_error("lookup failed");
    }

    int SelfTest()
    {
        using StaffSpellBridge::ResolveWith;

        Check(ResolveWith(nullptr, kStaffCopy) == kStaffCopy, "no DLL: a runtime form stays itself");
        Check(ResolveWith(nullptr, kPluginSpell) == kPluginSpell, "no DLL: a plugin spell stays itself");

        g_lookups = 0;
        Check(ResolveWith(FakeGetOriginal, kPluginSpell) == kPluginSpell, "DLL: a plugin spell stays itself");
        Check(ResolveWith(FakeGetOriginal, kLightSpell) == kLightSpell, "DLL: a light-plugin spell stays itself");
        Check(ResolveWith(FakeGetOriginal, kOriginal) == kOriginal, "DLL: a Skyrim.esm spell stays itself");
        Check(g_lookups == 0, "DLL: forms below index 0xFF are never asked about");

        Check(ResolveWith(FakeGetOriginal, kOtherRuntime) == kOtherRuntime,
            "DLL: a runtime form it does not know (0) stays itself");
        Check(ResolveWith(FakeGetOriginal, kStaffCopy) == kOriginal, "DLL: a staff copy becomes its original spell");
        Check(g_lookups == 2, "DLL: each runtime form asked once");

        // Resolve's catch turns the link off on a throw: ResolveWith must let it out, not terminate
        bool passedOut = false;
        try {
            (void)ResolveWith(ThrowingGetOriginal, kStaffCopy);
        } catch (const std::runtime_error&) {
            passedOut = true;
        }
        Check(passedOut, "DLL: a throw from the lookup reaches the caller");

        // SCSSoulCharge's staff-cast message (its src/api.h: type 'SCST', 12 bytes copy/spell/concentration)
        using StaffSpellBridge::DecodeStaffCast;
        using StaffSpellBridge::kStaffCastMessageType;
        const std::uint32_t message[3] = { kStaffCopy, kOriginal, 1 };
        const auto cast = DecodeStaffCast(kStaffCastMessageType, sizeof(message), message);
        Check(cast && cast->copy == kStaffCopy && cast->spell == kOriginal && cast->concentration == 1,
            "message: a staff cast decodes to its copy, spell and concentration flag");
        Check(kStaffCastMessageType == 0x53435354u, "message: the type is 'SCST' (0x53435354) as SCSSoulCharge sends it");
        Check(!DecodeStaffCast(kStaffCastMessageType, 8, message), "message: fewer than 12 bytes is ignored");
        const std::uint32_t longer[5] = { kStaffCopy, kOriginal, 0, 7, 9 };  // a later build with fields appended
        const auto appended = DecodeStaffCast(kStaffCastMessageType, sizeof(longer), longer);
        Check(appended && appended->copy == kStaffCopy && appended->spell == kOriginal && appended->concentration == 0,
            "message: a longer message (fields appended) gives its first 12 bytes");
        Check(!DecodeStaffCast(0x41424344u, sizeof(message), message), "message: another message type is ignored");
        Check(!DecodeStaffCast(kStaffCastMessageType, sizeof(message), nullptr), "message: no data is ignored");
        const std::uint32_t noSpell[3] = { kStaffCopy, 0, 0 };
        Check(!DecodeStaffCast(kStaffCastMessageType, sizeof(noSpell), noSpell), "message: no spell is ignored");

        // SCSSoulCharge's held-staff name marker (API 2): HoM's stored names never keep it
        using StaffSpellBridge::StripMarkerWith;
        constexpr std::string_view kMarker = " \xE2\x97\x88";  // " ◈" (U+25C8) in UTF-8 (SCSSoulCharge's INI default is U+2605)
        Check(StripMarkerWith(kMarker, "Healing \xE2\x97\x88") == "Healing", "marker: a trailing marker is taken off");
        Check(StripMarkerWith(kMarker, "Healing") == "Healing", "marker: a name without it stays");
        Check(StripMarkerWith(kMarker, "Healing (Learning - 35%) \xE2\x97\x88") == "Healing (Learning - 35%)",
            "marker: taken off after HoM's learning tag too");
        Check(StripMarkerWith(kMarker, "\xE2\x97\x88 Healing") == "\xE2\x97\x88 Healing", "marker: only at the end");
        Check(StripMarkerWith("", "Healing \xE2\x97\x88") == "Healing \xE2\x97\x88", "marker: an empty marker (off) changes nothing");
        Check(StripMarkerWith(kMarker, "\xE2\x97\x88") == "\xE2\x97\x88", "marker: a name shorter than the marker stays");
        using StaffSpellBridge::StripHeldMarkerWith;
        Check(StripHeldMarkerWith(kOriginal, kOriginal, kMarker, "Healing \xE2\x97\x88") == "Healing",
            "marker: taken off the held staff's spell");
        Check(StripHeldMarkerWith(kOriginal, kPluginSpell, kMarker, "Star \xE2\x97\x88") == "Star \xE2\x97\x88",
            "marker: another spell ending with the same characters keeps them");
        Check(StripHeldMarkerWith(0, kOriginal, kMarker, "Healing \xE2\x97\x88") == "Healing \xE2\x97\x88",
            "marker: no staff held (0) - nothing taken off");

        std::printf("staff-bridge-check: %d checks, %d failed\n", g_checks, g_failures);
        return g_failures == 0 ? kExitOk : kExitFailed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: staff-bridge-check --self-test\n");
        return kExitUsage;
    }
    return SelfTest();
}
