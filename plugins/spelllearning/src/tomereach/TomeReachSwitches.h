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
        kGeneralSale,
        kSpecialtyModVendor,
        kWideDrops,
        kSpecificLoot,
        kQuestReward,
        kCraftedOnly,
        kUncommonVendor,
        kReview,
    };

    inline constexpr std::size_t kSegmentCount = 9;

    namespace Detail
    {
        inline constexpr std::array<std::string_view, kSegmentCount> kSegmentNames{ "unreachable", "general sale",
            "specialty mod vendor", "wide drops", "specific loot", "quest reward", "crafted only", "uncommon vendor",
            "review" };
        inline constexpr std::array<std::string_view, kSegmentCount> kSegmentKeys{ "unreachable", "generalSale",
            "specialtyModVendor", "wideDrops", "specificLoot", "questReward", "craftedOnly", "uncommonVendor", "review" };
    }

    // For people (logs, the harness dump)
    [[nodiscard]] inline std::string_view SegmentName(Segment segment)
    {
        return Detail::kSegmentNames[static_cast<std::size_t>(segment)];
    }

    // Its id in the scan's source object and in config.json's tomeSources (generalSale, wideDrops ...)
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

    // Which segments go on the tree (the settings' switches). Unreachable never does; review stays on the
    // tree and Gate Review lists it, as the spell gate's review band
    struct SegmentSwitches
    {
        bool generalSale = true;
        bool wideDrops = true;
        bool uncommonVendors = true;
        bool specialtyModVendors = false;
        bool specificLoot = false;
        bool questRewards = false;
        bool craftedOnly = false;

        [[nodiscard]] bool OnTree(Segment segment) const
        {
            switch (segment) {
            case Segment::kGeneralSale: return generalSale;
            case Segment::kSpecialtyModVendor: return specialtyModVendors;
            case Segment::kWideDrops: return wideDrops;
            case Segment::kSpecificLoot: return specificLoot;
            case Segment::kQuestReward: return questRewards;
            case Segment::kCraftedOnly: return craftedOnly;
            case Segment::kUncommonVendor: return uncommonVendors;
            case Segment::kReview: return true;
            default: return false;
            }
        }

        // The switch of a segment that has one; false for unreachable and review
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
            case Segment::kGeneralSale: return &generalSale;
            case Segment::kSpecialtyModVendor: return &specialtyModVendors;
            case Segment::kWideDrops: return &wideDrops;
            case Segment::kSpecificLoot: return &specificLoot;
            case Segment::kQuestReward: return &questRewards;
            case Segment::kCraftedOnly: return &craftedOnly;
            case Segment::kUncommonVendor: return &uncommonVendors;
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
