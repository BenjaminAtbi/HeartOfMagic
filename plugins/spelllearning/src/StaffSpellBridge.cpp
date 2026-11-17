#include "StaffSpellBridge.h"
#include "Common.h"

#include <atomic>

// =============================================================================
// StaffSpellBridge - see StaffSpellBridge.h
// =============================================================================

namespace StaffSpellBridge
{
    namespace
    {
        using ApiVersionFn = std::uint32_t (*)();

        // Written once at kPostPostLoad, read from the cast event's thread, the game
        // thread and the effect hook's threads.
        std::atomic<GetOriginalSpellFn> g_getOriginal{ nullptr };
    }

    void Init()
    {
        const HMODULE module = GetModuleHandleW(L"SCSSoulCharge.dll");
        if (!module) {
            logger::info("StaffSpellBridge: SCSSoulCharge.dll is not loaded - staff casts are handled as before");
            return;
        }
        const auto apiVersion = reinterpret_cast<ApiVersionFn>(GetProcAddress(module, "SCSSoulCharge_ApiVersion"));
        const auto getOriginal = reinterpret_cast<GetOriginalSpellFn>(GetProcAddress(module, "SCSSoulCharge_GetOriginalSpell"));
        if (!apiVersion || !getOriginal) {
            logger::warn("StaffSpellBridge: SCSSoulCharge.dll has no original-spell lookup (an older build?) - "
                         "staff casts give no learning XP and are not weakened");
            return;
        }
        const std::uint32_t version = apiVersion();
        if (version < kMinApiVersion) {
            logger::warn("StaffSpellBridge: SCSSoulCharge.dll reports API version {} (needs {}) - link off",
                version, kMinApiVersion);
            return;
        }
        g_getOriginal.store(getOriginal, std::memory_order_release);
        logger::info("StaffSpellBridge: linked to SCSSoulCharge.dll (API {}) - a staff cast counts as its original spell "
                     "for learning XP and early-learned effectiveness", version);
    }

    std::uint32_t Resolve(std::uint32_t a_formID) noexcept
    {
        return ResolveWith(g_getOriginal.load(std::memory_order_acquire), a_formID);
    }
}
