#include "StaffSpellBridge.h"
#include "Common.h"
#include "SpellCastHandler.h"

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
        std::atomic<bool> g_listening{ false };  // set at kPostLoad, read on the game thread
        using GetNameMarkerFn = const char* (*)();
        using GetHeldStaffSpellFn = std::uint32_t (*)();
        // API 2; written once at kPostPostLoad, both set or both null
        std::atomic<GetNameMarkerFn> g_getNameMarker{ nullptr };
        std::atomic<GetHeldStaffSpellFn> g_getHeldSpell{ nullptr };
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
                         "staff casts give no learning XP and concentration staff effects are not weakened");
            return;
        }
        const std::uint32_t version = apiVersion();
        if (version < kMinApiVersion) {
            logger::warn("StaffSpellBridge: SCSSoulCharge.dll reports API version {} (needs {}) - link off",
                version, kMinApiVersion);
            return;
        }
        g_getOriginal.store(getOriginal, std::memory_order_release);
        if (version >= 2) {
            const auto getMarker = reinterpret_cast<GetNameMarkerFn>(GetProcAddress(module, "SCSSoulCharge_GetNameMarker"));
            const auto getHeld = reinterpret_cast<GetHeldStaffSpellFn>(GetProcAddress(module, "SCSSoulCharge_GetHeldStaffSpell"));
            if (getMarker && getHeld) {
                g_getHeldSpell.store(getHeld, std::memory_order_release);
                g_getNameMarker.store(getMarker, std::memory_order_release);
            }
        }
        logger::info("StaffSpellBridge: linked to SCSSoulCharge.dll (API {}) - a staff cast counts as its original spell "
                     "for learning XP and early-learned effectiveness", version);
    }

    void RegisterCastListener()
    {
        const HMODULE module = GetModuleHandleW(L"SCSSoulCharge.dll");
        if (!module) {
            return;  // Init logs the missing DLL
        }
        // The 'SCST' message comes with API 2. An older build sends none: listening to it would skip the copies'
        // cast events (the only report such a build gets) and count nothing, so the event path stays on
        const auto apiVersion = reinterpret_cast<std::uint32_t (*)()>(GetProcAddress(module, "SCSSoulCharge_ApiVersion"));
        const std::uint32_t version = apiVersion ? apiVersion() : 0;
        if (version < kMessageApiVersion) {
            logger::info("StaffSpellBridge: SCSSoulCharge.dll API {} sends no staff-cast message (needs {}) - staff casts "
                         "are counted from the cast event", version, kMessageApiVersion);
            return;
        }
        // Sent on the main thread from SCSSoulCharge's frame hook; HandlePlayerCast runs from the task queue as
        // the cast event's casts do
        const bool registered = SKSE::GetMessagingInterface()->RegisterListener(kStaffSender, [](SKSE::MessagingInterface::Message* a_msg) {
            // Called inside SCSSoulCharge's frame hook through SKSE's Dispatch: nothing may throw out of here
            static std::atomic<bool> failureReported{ false };
            try {
                if (!a_msg) return;
                if (const auto cast = DecodeStaffCast(a_msg->type, a_msg->dataLen, a_msg->data)) {
                    SpellCastHandler::GetSingleton()->OnStaffCast(cast->copy, cast->spell);
                }
            } catch (...) {
                if (!failureReported.exchange(true)) {
                    logger::error("StaffSpellBridge: handling a staff-cast message threw; further failures are not logged");
                }
            }
        });
        g_listening.store(registered, std::memory_order_release);
        if (registered) {
            logger::info("StaffSpellBridge: listening for SCSSoulCharge staff casts (a staff cast may send no cast event; the message is counted)");
        } else {
            logger::warn("StaffSpellBridge: could not listen to SCSSoulCharge messages - staff casts give no learning XP");
        }
    }

    std::string StripNameMarker(std::uint32_t a_spell, std::string_view a_name)
    {
        const GetNameMarkerFn getMarker = g_getNameMarker.load(std::memory_order_acquire);
        const GetHeldStaffSpellFn getHeld = g_getHeldSpell.load(std::memory_order_acquire);
        if (!getMarker || !getHeld) return std::string(a_name);
        const std::uint32_t held = getHeld();
        if (held == 0 || held != a_spell) return std::string(a_name);
        const char* marker = getMarker();  // the DLL's own string: read here, not kept
        return StripHeldMarkerWith(held, a_spell, marker ? std::string_view(marker) : std::string_view(), a_name);
    }

    bool IsListening() noexcept
    {
        return g_listening.load(std::memory_order_acquire);
    }

    std::uint32_t Resolve(std::uint32_t a_formID) noexcept
    {
        const GetOriginalSpellFn getOriginal = g_getOriginal.load(std::memory_order_acquire);
        try {
            return ResolveWith(getOriginal, a_formID);
        } catch (...) {
            // Callers sit in engine hooks and on effect threads: drop the link, keep the game
            if (g_getOriginal.exchange(nullptr, std::memory_order_acq_rel) != nullptr) {
                logger::error("StaffSpellBridge: SCSSoulCharge_GetOriginalSpell threw for {:08X} - link off for "
                              "this session, staff casts are handled as before", a_formID);
            }
            return a_formID;
        }
    }
}
