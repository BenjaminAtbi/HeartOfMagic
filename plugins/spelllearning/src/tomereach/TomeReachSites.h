#pragma once

// =============================================================================
// TOME REACH - SITES AND REGIONS (distribution model)
// =============================================================================
//
// Where the bases of the reach graph are placed, counted by (region, site). A
// site is the location under its hold (a dungeon, a town), its region the hold,
// the top location or the worldspace; a mod's own worldspace or location tree
// outside the holds (added by a plugin other than the base game's) is one region,
// "realm <plugin>". Placements in developer test cells are dropped. Port of the
// author's Python model (scripts/spellmap/reach.py, Sites). Pure C++, shared by
// the plugin and tools/tome-reach-test.

#include "tomereach/TomeReachGraph.h"

#include <compare>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace TomeReach
{
    struct RegionSite
    {
        std::string region;
        std::string site;
        friend auto operator<=>(const RegionSite&, const RegionSite&) = default;
    };

    struct Placements
    {
        std::size_t count = 0;                       // placements outside test cells
        std::map<RegionSite, std::size_t> sites;     // per (region, site)

        void Add(const Placements& other);
    };

    class Sites
    {
    public:
        Sites(const SpellStore& store, const NameTable& names, const ReachGraph& graph);

        // Placements of one base (references whose winning version places it, not deleted)
        const Placements& OfBase(Key base);
        Placements Of(const std::vector<Key>& bases);
        // Placements of an NPC through every base that places it (ReachGraph::NpcBases; inventory: the bases that
        // place its inventory), and where whatever spawns it is: the reference a script sits on, the records that
        // fire a spawning explosion. A spawner placed nowhere known stands in as a site ("spawned", its label), only
        // when the NPC has no other placement
        const Placements& OfNpc(Key npc, bool inventory = false);
        [[nodiscard]] RegionSite Where(const PlacedRef& ref) const;

    private:
        Placements OfNpc(Key npc, bool inventory, const std::unordered_set<Key>& seen);
        [[nodiscard]] std::string StandIn(const Spawner& spawner) const;

        [[nodiscard]] bool IsBasePlugin(Key form) const;
        [[nodiscard]] std::string Realm(Key form) const;

        const SpellStore& m_store;
        const NameTable& m_names;
        const ReachGraph& m_graph;
        std::unordered_map<Key, std::vector<const PlacedRef*>> m_byBase;
        std::unordered_map<Key, Placements> m_ofBase;
        std::unordered_map<Key, Placements> m_ofNpc;            // whole NPCs
        std::unordered_map<Key, Placements> m_ofNpcInventory;   // their inventories
    };

    // The region of a stand-in site: an NPC spawned by something placed nowhere known
    inline constexpr std::string_view kSpawnedRegion = "spawned";

    // A form as text: plugin:LOCALID (plugin name lower case, local id as six hex digits)
    [[nodiscard]] std::string KeyText(const NameTable& names, Key key);
    // A test cell's site name (TestCell anywhere, case ignored; or QA and a capital or an underscore first, as
    // written: Vominheim's QAlderim is not one)
    [[nodiscard]] bool IsTestCellName(std::string_view site);
}
