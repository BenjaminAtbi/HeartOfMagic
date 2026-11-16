#pragma once

// =============================================================================
// TOME REACH - THE SEGMENTS AND THEIR TREE SWITCHES
// =============================================================================
//
// The distribution segments a spell with a tome falls in (TomeReachSegments.h
// sorts them), their ids, and which of them go on the tree: config.json
// "tomeSources" ({ "<SegmentKey>": bool }), the defaults for what is missing.
// Header-only and free of game types: the scan (TomeReach.cpp), Tree Graft
// (GraftCandidates.h) and the offline tools read the same switches. The panel's
// counterpart is modules/tomeSourceSetting.js (same keys, same defaults).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace TomeReach
{
    enum class Segment : std::uint8_t
    {
        kUnreachable,
        kSpellpackVendor,
        kRitualQuest,
        kStandardSale,
        kStandardLoot,
        kRegionalLoot,
        kUniqueVendor,
        kUniqueReward,
        kCrafted,
        kReview,
    };

    inline constexpr std::size_t kSegmentCount = 10;

    namespace Detail
    {
        inline constexpr std::array<std::string_view, kSegmentCount> kSegmentNames{ "unreachable", "spellpack vendor",
            "ritual quest", "standard sale", "standard loot", "regional loot", "unique vendor", "unique reward", "crafted",
            "review" };
        inline constexpr std::array<std::string_view, kSegmentCount> kSegmentKeys{ "unreachable", "spellpackVendor",
            "ritualQuest", "standardSale", "standardLoot", "regionalLoot", "uniqueVendor", "uniqueReward", "crafted",
            "review" };
    }

    // For people (logs, the harness dump)
    [[nodiscard]] inline std::string_view SegmentName(Segment segment)
    {
        return Detail::kSegmentNames[static_cast<std::size_t>(segment)];
    }

    // Its id in the scan's source object and in config.json's tomeSources (standardSale, standardLoot ...)
    [[nodiscard]] inline std::string_view SegmentKey(Segment segment)
    {
        return Detail::kSegmentKeys[static_cast<std::size_t>(segment)];
    }

    // The segment of an id SegmentKey writes; nullopt for anything else
    [[nodiscard]] inline std::optional<Segment> SegmentOfKey(std::string_view key)
    {
        for (std::size_t i = 0; i < kSegmentCount; ++i) {
            if (Detail::kSegmentKeys[i] == key) return static_cast<Segment>(i);
        }
        return std::nullopt;
    }

    // Which segments go on the tree (the settings' switches). Review has no switch of its own: every
    // spell is meant to fall in one of the other segments, and one that falls through goes with
    // unreachable (off by default; the panel shows both as its "Unreachable" row)
    struct SegmentSwitches
    {
        bool unreachable = false;
        bool spellpackVendor = false;
        bool ritualQuest = true;
        bool standardSale = true;
        bool standardLoot = true;
        bool regionalLoot = false;
        bool uniqueVendor = false;
        bool uniqueReward = false;
        bool crafted = false;

        [[nodiscard]] bool OnTree(Segment segment) const
        {
            switch (segment) {
            case Segment::kSpellpackVendor: return spellpackVendor;
            case Segment::kRitualQuest: return ritualQuest;
            case Segment::kStandardSale: return standardSale;
            case Segment::kStandardLoot: return standardLoot;
            case Segment::kRegionalLoot: return regionalLoot;
            case Segment::kUniqueVendor: return uniqueVendor;
            case Segment::kUniqueReward: return uniqueReward;
            case Segment::kCrafted: return crafted;
            default: return unreachable;   // kUnreachable, kReview
            }
        }

        // The switch of a segment that has one; false for review (it follows unreachable's)
        bool Set(Segment segment, bool on)
        {
            bool* which = Switch(segment);
            if (!which) return false;
            *which = on;
            return true;
        }

    private:
        bool* Switch(Segment segment)
        {
            switch (segment) {
            case Segment::kUnreachable: return &unreachable;
            case Segment::kSpellpackVendor: return &spellpackVendor;
            case Segment::kRitualQuest: return &ritualQuest;
            case Segment::kStandardSale: return &standardSale;
            case Segment::kStandardLoot: return &standardLoot;
            case Segment::kRegionalLoot: return &regionalLoot;
            case Segment::kUniqueVendor: return &uniqueVendor;
            case Segment::kUniqueReward: return &uniqueReward;
            case Segment::kCrafted: return &crafted;
            default: return nullptr;
            }
        }
    };

    // The switches of a whole config.json: its "tomeSources" object, booleans only, the defaults for the rest
    [[nodiscard]] inline SegmentSwitches SegmentSwitchesOf(const nlohmann::json& config)
    {
        SegmentSwitches switches;
        if (!config.is_object()) return switches;
        const auto section = config.find("tomeSources");
        if (section == config.end() || !section->is_object()) return switches;
        for (std::size_t i = 0; i < kSegmentCount; ++i) {
            const auto segment = static_cast<Segment>(i);
            const auto value = section->find(std::string(SegmentKey(segment)));
            if (value != section->end() && value->is_boolean()) switches.Set(segment, value->get<bool>());
        }
        return switches;
    }
}
