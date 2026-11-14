#pragma once

// =============================================================================
// TOME REACH - THE REACH GRAPH (distribution model)
// =============================================================================
//
// Where an item enters the game, from the spell store: every form's parents
// (leveled lists holding it, containers and NPCs carrying it, NPCs dropping it on
// death), the walk from an item up to its roots (the containers and NPCs above it
// through nested leveled lists), the bases that place an NPC (leveled NPC lists
// and NPCs templated on it), vendor chests, recipes and quest alias rewards.
// Containers and NPCs whose editor id marks a test or debug record are left out.
// Port of the author's Python model (scripts/spellmap/reach.py, Graph), without
// the configs and scripts of stage 3 (docs/superpowers/specs/2026-11-14-tome-
// distribution-design.md). Pure C++, shared by the plugin and tools/tome-reach-test.

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

    class ReachGraph
    {
    public:
        explicit ReachGraph(const SpellStore& store);

        [[nodiscard]] Roots RootsOf(Key item) const;
        // Every base that places NPC `npc`: itself, leveled NPC lists holding it, NPCs templated on it (recursive)
        [[nodiscard]] std::vector<Key> NpcBases(Key npc) const;
        // A merchant chest: a faction's VENC reference's base, or a container whose editor id says so
        [[nodiscard]] bool IsVendor(Key container) const;
        // The quests whose aliases create or hold `item`
        [[nodiscard]] const std::vector<Key>& RewardsOf(Key item) const;
        // The recipes that make `item`
        [[nodiscard]] const std::vector<Key>& RecipesOf(Key item) const;
        // A recipe only the non-player can use (GetIsID Player == 0): it never shows
        [[nodiscard]] bool RecipeDisabled(Key recipe) const;

    private:
        enum class EdgeKind : std::uint8_t { kList, kContainer, kNpc };

        struct Edge
        {
            EdgeKind kind;
            Key parent;
            friend bool operator==(const Edge&, const Edge&) = default;
        };

        void AddEdge(Key child, EdgeKind kind, Key parent);
        [[nodiscard]] const std::string& EditorId(Key form) const;

        const SpellStore& m_store;
        std::unordered_map<Key, std::vector<Edge>> m_parents;
        std::unordered_map<Key, std::vector<Key>> m_npcParents;   // NPC_/LVLN -> LVLN holding it, NPC_ templated on it
        std::unordered_map<Key, std::vector<Key>> m_rewards;
        std::unordered_map<Key, std::vector<Key>> m_recipes;
        std::unordered_set<Key> m_vendorBases;
    };

    // Editor ids of test and debug records: containers and NPCs that hand nothing to the player
    [[nodiscard]] bool IsNoiseEditorId(std::string_view editorId);
}
