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
