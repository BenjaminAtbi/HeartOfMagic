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
        // Placements of an NPC through every base that places it (ReachGraph::NpcBases)
        const Placements& OfNpc(Key npc);
        [[nodiscard]] RegionSite Where(const PlacedRef& ref) const;

    private:
        [[nodiscard]] bool IsBasePlugin(Key form) const;
        [[nodiscard]] std::string Realm(Key form) const;

        const SpellStore& m_store;
        const NameTable& m_names;
        const ReachGraph& m_graph;
        std::unordered_map<Key, std::vector<const PlacedRef*>> m_byBase;
        std::unordered_map<Key, Placements> m_ofBase;
        std::unordered_map<Key, Placements> m_ofNpc;
    };

    // A form as text: plugin:LOCALID (plugin name lower case, local id as six hex digits)
    [[nodiscard]] std::string KeyText(const NameTable& names, Key key);
    // A test cell's site name (TestCell anywhere, or QA and a letter first; case ignored)
    [[nodiscard]] bool IsTestCellName(std::string_view site);
}
