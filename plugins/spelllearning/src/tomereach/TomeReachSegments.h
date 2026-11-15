#pragma once

// =============================================================================
// TOME REACH - SPELLPACK VENDORS AND SEGMENTS (distribution model)
// =============================================================================
//
// Every spell with a tome falls in one segment, first match winning: no route
// (unreachable), spellpack vendor, ritual quest, standard sale, standard loot,
// regional loot, unique vendor, unique reward, crafted, review. The settings
// switch each segment on or off the tree.
// - A spellpack vendor is up to three vendor chests that sell 75%+ of a plugin's
//   vendor-sold tome spells through entries dedicated to that plugin; its spells
//   count when they have three open vendors or fewer.
// - A vendor's stock is gated when it comes through a list held at 100% chance
//   of nothing by a global: MGRitual* globals (the College's master rituals) make
//   ritual quest, PC* globals (skill thresholds) count as open, others as quest.
// - Standard sale: 3+ open vendors. Unique vendor: 1-2, or quest-gated stock.
// Port of the author's Python (scripts/spellmap/shops.py --buckets).
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

    // Up to three chests that sell most of a plugin's vendor-sold tome spells
    struct SpellpackShop
    {
        std::string plugin;          // lower case
        std::vector<Key> chests;     // the chests that sell the most of the set (1-3)
        std::size_t set = 0;         // the plugin's vendor-sold tome spells
        std::size_t sold = 0;        // of them, sold by those chests
    };

    struct SpellSource
    {
        Segment segment = Segment::kUnreachable;
        Route best;                  // the most generic route of its tomes
        std::vector<Key> tomes;
        std::string shop;            // spellpack vendor: the plugin whose set it belongs to
    };

    struct SpellSources
    {
        std::map<Key, SpellSource> spells;
        std::vector<SpellpackShop> shops;   // by plugin name
        std::map<Key, TomeSource> tomes;
    };

    // Every spell of the store's tomes, with its segment
    [[nodiscard]] SpellSources ClassifySpells(const SpellStore& store, const NameTable& names, Classifier& classifier);
}
