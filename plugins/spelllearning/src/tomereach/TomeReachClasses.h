#pragma once

// =============================================================================
// TOME REACH - DISTRIBUTION CLASSES (distribution model)
// =============================================================================
//
// How each tome enters the game, as routes of a class and subtype, the most
// generic first: generic (a shared list on 5+ vendor chests, in containers of 3+
// regions, on 100+ NPCs of mixed factions), faction (3+ NPCs or containers of one
// faction or region), vendor (1-4 vendor chests), encounter (one or two sites),
// placed, reward, crafted, other (a SPID line whose filters can't be judged) and
// none. Port of the author's Python (scripts/spellmap/distribution.py). Pure C++,
// shared by the plugin and tools/tome-reach-test.

#include "tomereach/TomeReachDistr.h"
#include "tomereach/TomeReachGraph.h"
#include "tomereach/TomeReachSites.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace TomeReach
{
    // Most generic first: a spell's class is its tomes' lowest
    enum class SourceClass : std::uint8_t
    {
        kGeneric,
        kFaction,
        kVendor,
        kEncounter,
        kPlaced,
        kReward,
        kCrafted,
        kOther,
        kNone,
    };

    [[nodiscard]] std::string_view ClassName(SourceClass cls);

    struct Route
    {
        SourceClass cls = SourceClass::kNone;
        std::string subtype;   // vendors, loot, npcs, spid ...; faction / local / regional; boss; the recipe's bench
        std::string detail;    // for people: vendors, sites, factions, SPID filters, recipe

        // Python's tuple order: class, then subtype, then detail
        [[nodiscard]] bool operator<(const Route& other) const;
    };

    // A vendor chest above a tome, with the entries it holds that lead to the tome (the tome or leveled lists)
    struct VendorEntries
    {
        Key chest = 0;
        std::vector<Key> entries;
    };

    struct TomeSource
    {
        std::vector<Route> routes;            // never empty: none when nothing hands the tome out
        std::vector<VendorEntries> vendors;   // sorted by the chest's editor id
    };

    class Classifier
    {
    public:
        Classifier(const SpellStore& store, const ReachGraph& graph, Sites& sites, const Distribution* distribution);

        [[nodiscard]] TomeSource Of(Key tome);

    private:
        const std::vector<Route>& FromRoots(const std::vector<Key>& vendors, const std::vector<Key>& containers,
            const std::vector<Key>& npcs);
        [[nodiscard]] const std::string& EditorId(Key form) const;

        const SpellStore& m_store;
        const ReachGraph& m_graph;
        Sites& m_sites;
        const Distribution* m_distribution;
        std::map<std::vector<Key>, std::vector<Route>> m_memo;   // per root set (vendors, containers, NPCs)
    };
}
