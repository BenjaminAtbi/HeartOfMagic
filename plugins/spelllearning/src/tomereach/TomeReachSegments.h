#pragma once

// =============================================================================
// TOME REACH - SPECIALTY MOD VENDORS AND SEGMENTS (distribution model)
// =============================================================================
//
// Every spell with a tome falls in one segment, first match winning: no route
// (unreachable), general sale, specialty mod vendor, wide drops, specific loot,
// quest reward, crafted only, uncommon vendor, review. The settings switch each
// segment on or off the tree. A specialty mod vendor is one or two vendor chests
// that sell 75%+ of a plugin's vendor-sold tome spells through entries dedicated
// to that plugin. Port of the author's Python (scripts/spellmap/shops.py; its
// "specific" bucket is split into specific loot, quest reward and crafted only).
// Pure C++, shared by the plugin and tools/tome-reach-test.

#include "tomereach/TomeReachClasses.h"
#include "tomereach/TomeReachSwitches.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace TomeReach
{
    // Segment, SegmentKey and SegmentSwitches: TomeReachSwitches.h

    // One or two chests that sell most of a plugin's vendor-sold tome spells
    struct SpecialtyShop
    {
        std::string plugin;          // lower case
        std::vector<Key> chests;     // the chests that sell the most of the set (1-2)
        std::size_t set = 0;         // the plugin's vendor-sold tome spells
        std::size_t sold = 0;        // of them, sold by those chests
    };

    struct SpellSource
    {
        Segment segment = Segment::kUnreachable;
        Route best;                  // the most generic route of its tomes
        std::vector<Key> tomes;
        std::string shop;            // specialty mod vendor: the plugin whose set it belongs to
    };

    struct SpellSources
    {
        std::map<Key, SpellSource> spells;
        std::vector<SpecialtyShop> shops;   // by plugin name
        std::map<Key, TomeSource> tomes;
    };

    // Every spell of the store's tomes, with its segment
    [[nodiscard]] SpellSources ClassifySpells(const SpellStore& store, const NameTable& names, Classifier& classifier);
}
