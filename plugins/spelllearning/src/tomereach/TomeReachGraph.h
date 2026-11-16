#pragma once

// =============================================================================
// TOME REACH - THE REACH GRAPH (distribution model)
// =============================================================================
//
// Where an item enters the game, from the spell store: every form's parents
// (leveled lists holding it, containers and NPCs carrying it, NPCs dropping it on
// death), the walk from an item up to its roots (the containers and NPCs above it
// through nested leveled lists), the bases that place an NPC (leveled NPC lists
// and NPCs templated on it), vendor chests, recipes and rewards.
// - An NPC carries its effective inventory: its inventory (CNTO) and default
//   outfit (DOFT) come from its template when the template flag Use Inventory is
//   set, its death item (INAM) when Use Traits is (a leveled NPC template: any of
//   its members').
// - With the configs (TomeReachDistr.h): SPID lines are roots of their own,
//   SkyPatcher addToLLs / addOnceToLLs add list parents and removeFromLLs takes
//   them away.
// - With the data files (TomeReachScripts.h): a script's `List.AddForm(Item)`,
//   both from its properties, adds a list parent (a leveled list) or a run-time
//   member (a form list); a property that reaches AddItem or PlaceAtMe is a
//   reward; one that reaches PlaceAtMe / PlaceActorAtMe spawns the NPC it holds.
//   Scripts are read on records (the holder types, dialogue, scenes, packages)
//   and on placed references.
// - Spawned NPCs (an explosion's placed object, a quest alias that creates one,
//   a script) have spawners: Sites counts them where their creator is.
// Containers, NPCs and script holders whose editor id marks a test or debug
// record are left out. Port of the author's Python model
// (scripts/spellmap/reach.py, Graph; docs/superpowers/specs/2026-11-14-tome-
// distribution-design.md). Pure C++, shared by the plugin and tools/tome-reach-test.

#include "tomereach/TomeReachData.h"
#include "tomereach/TomeReachDistr.h"
#include "tomereach/TomeReachReader.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TomeReach
{
    enum class RootKind : std::uint8_t
    {
        kContainer,   // CONT inventory
        kNpc,         // NPC_ inventory, default outfit or death item
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

    // Who hands an item out: a script on a record or placed reference (AddItem, PlaceAtMe), or a quest alias
    // (created or held). holder: the record, or the first of the placed references that share a label
    struct Reward
    {
        Key holder;
        bool alias;

        friend bool operator==(const Reward&, const Reward&) = default;
        friend auto operator<=>(const Reward&, const Reward&) = default;
    };

    // What creates an NPC at run time
    enum class SpawnKind : std::uint8_t
    {
        kAlias,       // a quest alias creates it (creator: the quest)
        kExplosion,   // an explosion places it (creator: the EXPL)
        kRef,         // a script on a placed reference (creator: the reference): it appears there
        kScript,      // a script on a record (creator: the record)
    };

    struct Spawner
    {
        SpawnKind kind;
        Key creator;
        std::string label;   // the stand-in site's name when the creator is placed nowhere known

        friend bool operator==(const Spawner&, const Spawner&) = default;
        friend auto operator<=>(const Spawner&, const Spawner&) = default;
    };

    class ReachGraph
    {
    public:
        // `distribution` and `data` null: records only
        explicit ReachGraph(const SpellStore& store, const Distribution* distribution = nullptr,
            const DataFiles* data = nullptr);

        [[nodiscard]] Roots RootsOf(Key item) const;
        // Every base that places NPC `npc`: itself, leveled NPC lists holding it, NPCs templated on it (recursive).
        // inventory: the bases that place npc's inventory, leveled NPC lists only (template children carry their
        // own effective inventory)
        [[nodiscard]] std::vector<Key> NpcBases(Key npc, bool inventory = false) const;
        // The containers that hold `item` in their inventory (CNTO)
        [[nodiscard]] std::vector<Key> HoldersOf(Key item) const;
        // The leveled lists that hold `item` (entries, SkyPatcher and script additions)
        [[nodiscard]] std::vector<Key> ListsHolding(Key item) const;
        // A merchant chest: a faction's VENC reference's base, or a placed container whose editor id says so
        [[nodiscard]] bool IsVendor(Key container) const;
        // The scripts and quest aliases that hand `item` out
        [[nodiscard]] const std::vector<Reward>& RewardsOf(Key item) const;
        // What creates NPC_ / LVLN `npc` at run time, sorted
        [[nodiscard]] const std::vector<Spawner>& SpawnersOf(Key npc) const;
        // Records whose winning version fires `explosion` from a destruction stage (DSTD), sorted
        [[nodiscard]] const std::vector<Key>& ExplosionUsers(Key explosion) const;
        // A script holder's name for people: its editor id, or the placed references' label
        [[nodiscard]] std::string HolderLabel(Key holder) const;
        // The recipes that make `item`
        [[nodiscard]] const std::vector<Key>& RecipesOf(Key item) const;
        // A recipe only the non-player can use (GetIsID Player == 0): it never shows
        [[nodiscard]] bool RecipeDisabled(Key recipe) const;

        [[nodiscard]] std::size_t ScriptsRead() const { return m_scriptsRead; }
        [[nodiscard]] std::size_t ScriptsMissing() const { return m_scriptsMissing; }

    private:
        enum class EdgeKind : std::uint8_t { kList, kContainer, kNpc, kSpid };
        enum class NpcPart : std::uint8_t { kInventory, kOutfit, kDeathItem };
        static constexpr std::size_t kNpcParts = 3;

        struct Edge
        {
            EdgeKind kind;
            Key parent;
            friend bool operator==(const Edge&, const Edge&) = default;
        };

        // One set of scripts: a record's, or the placed references sharing a label (base and cell)
        struct ScriptSource
        {
            Key holder;                                   // the record, or the first reference
            std::vector<Key> refs;                        // placed references (empty for a record)
            std::string label;
            bool noise;
            const std::vector<AttachedScript>* scripts;
        };

        void AddEdge(Key child, EdgeKind kind, Key parent);
        void RemoveEdge(Key child, EdgeKind kind, Key parent);
        // The items an NPC carries in one part, templates resolved (Python npc_items); top-level results cached
        [[nodiscard]] const std::unordered_set<Key>& NpcItems(Key npc, NpcPart part) const;
        [[nodiscard]] std::unordered_set<Key> CollectNpcItems(Key npc, NpcPart part, std::unordered_set<Key> seen) const;
        [[nodiscard]] std::vector<ScriptSource> ScriptSources();   // TomeReachScripts.cpp
        void AddScripts(const DataFiles& data);                   // TomeReachScripts.cpp
        void AddSpawners(const std::vector<ScriptSource>& sources, const std::vector<std::vector<Key>>& spawned);
        // A property's forms, form lists expanded (with the items scripts AddForm into them at run time, when
        // `runtime`)
        void Flat(const std::vector<Key>& forms, bool runtime, std::vector<Key>& out) const;
        void FormListMembers(Key list, std::unordered_set<Key>& seen, std::vector<Key>& out) const;
        [[nodiscard]] const std::string& EditorId(Key form) const;
        [[nodiscard]] bool KindIs(Key form, Kind kind) const;

        const SpellStore& m_store;
        std::unordered_map<Key, std::vector<Edge>> m_parents;
        std::unordered_map<Key, std::vector<Key>> m_npcParents;   // NPC_/LVLN -> LVLN holding it, NPC_ templated on it
        std::unordered_map<Key, std::vector<Reward>> m_rewards;
        std::unordered_map<Key, std::vector<Spawner>> m_spawners;
        std::unordered_map<Key, std::vector<Key>> m_explosionUsers;
        std::unordered_map<Key, std::vector<Key>> m_runtimeLists;   // FLST -> items scripts AddForm into it
        std::unordered_map<Key, std::string> m_holderLabels;        // reference groups' and other holders' labels
        std::unordered_map<Key, std::vector<Key>> m_recipes;
        std::unordered_set<Key> m_vendorBases;
        std::unordered_set<Key> m_namedVendors;
        mutable std::unordered_map<Key, std::unordered_set<Key>> m_npcItems[kNpcParts];   // per NpcPart: NPC -> items
        std::size_t m_scriptsRead = 0;
        std::size_t m_scriptsMissing = 0;
    };
}
