#pragma once

// =============================================================================
// TOME REACH - THE REACH GRAPH (distribution model)
// =============================================================================
//
// Where an item enters the game, from the spell store: every form's parents
// (leveled lists holding it, containers and NPCs carrying it, NPCs dropping it on
// death), the walk from an item up to its roots (the containers and NPCs above it
// through nested leveled lists), the bases that place an NPC (leveled NPC lists
// and NPCs templated on it), vendor chests, recipes and rewards. With the configs
// (TomeReachDistr.h): SPID lines are roots of their own and SkyPatcher adds list
// parents; with the data files (TomeReachScripts.h): scripts that AddForm their
// properties into their own leveled lists add list parents, and AddItem
// properties are rewards. Containers and NPCs whose editor id marks a test or
// debug record are left out. Port of the author's Python model
// (scripts/spellmap/reach.py, Graph; docs/superpowers/specs/2026-11-14-tome-
// distribution-design.md). Pure C++, shared by the plugin and tools/tome-reach-test.

#include "tomereach/TomeReachData.h"
#include "tomereach/TomeReachDistr.h"
#include "tomereach/TomeReachReader.h"

#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TomeReach
{
    enum class RootKind : std::uint8_t
    {
        kContainer,   // CONT inventory
        kNpc,         // NPC_ inventory or death item
        kSpid,        // a SPID Item / DeathItem line (form: its index in Distribution::spid)
    };

    struct Root
    {
        RootKind kind;
        Key form;

        friend bool operator==(const Root&, const Root&) = default;
        friend auto operator<=>(const Root&, const Root&) = default;
    };

    // The roots an item reaches, and the leveled lists passed on the way (the item itself included)
    struct Roots
    {
        std::vector<Root> roots;   // sorted, unique
        std::vector<Key> lists;    // sorted, unique
    };

    // Who hands an item out: a script on a record (AddItem), or a quest alias (created or held)
    struct Reward
    {
        Key holder;
        bool alias;

        friend bool operator==(const Reward&, const Reward&) = default;
        friend auto operator<=>(const Reward&, const Reward&) = default;
    };

    class ReachGraph
    {
    public:
        // `distribution` and `data` null: records only
        explicit ReachGraph(const SpellStore& store, const Distribution* distribution = nullptr,
            const DataFiles* data = nullptr);

        [[nodiscard]] Roots RootsOf(Key item) const;
        // Every base that places NPC `npc`: itself, leveled NPC lists holding it, NPCs templated on it (recursive)
        [[nodiscard]] std::vector<Key> NpcBases(Key npc) const;
        // A merchant chest: a faction's VENC reference's base, or a container whose editor id says so
        [[nodiscard]] bool IsVendor(Key container) const;
        // The scripts and quest aliases that hand `item` out
        [[nodiscard]] const std::vector<Reward>& RewardsOf(Key item) const;
        // The recipes that make `item`
        [[nodiscard]] const std::vector<Key>& RecipesOf(Key item) const;
        // A recipe only the non-player can use (GetIsID Player == 0): it never shows
        [[nodiscard]] bool RecipeDisabled(Key recipe) const;

        [[nodiscard]] std::size_t ScriptsRead() const { return m_scriptsRead; }
        [[nodiscard]] std::size_t ScriptsMissing() const { return m_scriptsMissing; }

    private:
        enum class EdgeKind : std::uint8_t { kList, kContainer, kNpc, kSpid };

        struct Edge
        {
            EdgeKind kind;
            Key parent;
            friend bool operator==(const Edge&, const Edge&) = default;
        };

        void AddEdge(Key child, EdgeKind kind, Key parent);
        void AddScripts(const DataFiles& data);   // TomeReachScripts.cpp
        void FormListMembers(Key list, std::unordered_set<Key>& seen, std::vector<Key>& out) const;
        [[nodiscard]] const std::string& EditorId(Key form) const;

        const SpellStore& m_store;
        std::unordered_map<Key, std::vector<Edge>> m_parents;
        std::unordered_map<Key, std::vector<Key>> m_npcParents;   // NPC_/LVLN -> LVLN holding it, NPC_ templated on it
        std::unordered_map<Key, std::vector<Reward>> m_rewards;
        std::unordered_map<Key, std::vector<Key>> m_recipes;
        std::unordered_set<Key> m_vendorBases;
        std::size_t m_scriptsRead = 0;
        std::size_t m_scriptsMissing = 0;
    };

    // Editor ids of test and debug records: containers and NPCs that hand nothing to the player
    [[nodiscard]] bool IsNoiseEditorId(std::string_view editorId);
}
