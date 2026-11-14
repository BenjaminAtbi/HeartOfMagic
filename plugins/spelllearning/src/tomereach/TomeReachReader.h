#pragma once

// =============================================================================
// TOME REACH - THE SPELL STORE (distribution model)
// =============================================================================
//
// The winning version of every record the distribution model reads, and where
// containers, NPCs and tome-carrying items are placed: the inputs of the graph
// (docs/superpowers/specs/2026-11-14-tome-distribution-design.md). A later
// plugin's version of a record replaces the earlier one whole; a cell's fields
// are merged version by version (a later version that leaves out its location or
// worldspace keeps the earlier one's). Pure C++, shared by the plugin and
// tools/tome-reach-test.

#include "tomereach/TomeReachRecords.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace TomeReach
{
    enum class Kind : std::uint8_t
    {
        kBook,        // BOOK that teaches a spell
        kScroll,      // SCRL
        kStaff,       // WEAP with the staff animation
        kItemList,    // LVLI
        kNpcList,     // LVLN
        kContainer,   // CONT
        kNpc,         // NPC_
        kOutfit,      // OTFT
        kFaction,     // FACT
        kRecipe,      // COBJ
        kQuest,       // QUST
    };

    struct Npc
    {
        std::vector<Key> items;      // CNTO
        Key deathItem = 0;           // INAM
        Key outfit = 0;              // DOFT
        Key templ = 0;               // TPLT
        std::vector<Key> factions;   // SNAM
        std::uint32_t flags = 0;     // ACBS flags (0x20: unique)
    };

    struct Recipe
    {
        Key created = 0;                  // CNAM
        Key bench = 0;                    // BNAM
        std::vector<Key> components;      // CNTO
        std::uint32_t plugin = 0;         // the winning version's plugin: resolves the conditions' forms
        std::vector<std::vector<unsigned char>> conditions;   // CTDA, as written
    };

    struct PlacedRef
    {
        Key base = 0;
        Key cell = 0;
        std::uint32_t flags = 0;     // record flags of the winning version (kDeletedRecord, kDisabledRecord)
    };

    struct Cell
    {
        Key worldspace = 0;          // exterior cells: their worldspace
        Key location = 0;            // XLCN
        bool interior = false;
        std::string editorId;
    };

    struct Location
    {
        Key parent = 0;              // PNAM
        std::string editorId;
    };

    struct SpellStore
    {
        std::unordered_map<Key, Kind> kinds;               // every record below, by its kind
        std::unordered_map<Key, std::string> editorIds;     // EDID of every record below
        std::unordered_map<Key, Key> bookSpells;            // tome -> the spell it teaches
        std::unordered_map<Key, std::vector<Key>> lists;    // LVLI and LVLN entries
        std::unordered_map<Key, std::vector<Key>> inventories;   // CONT CNTO, OTFT INAM
        std::unordered_map<Key, Npc> npcs;
        std::unordered_map<Key, Key> vendorChests;          // FACT -> its VENC reference
        std::unordered_map<Key, Recipe> recipes;
        std::unordered_map<Key, std::vector<Key>> questItems;   // QUST alias ALCO and CNTO
        // References whose base is a container, NPC, leveled NPC list, tome, scroll or
        // staff: the winning version of each
        std::unordered_map<Key, PlacedRef> refs;
        std::unordered_map<Key, Cell> cells;
        std::unordered_map<Key, Location> locations;
        std::unordered_map<Key, std::string> worldspaces;   // WRLD -> its editor id
        std::vector<std::string> unreadable;
        std::vector<std::string> damaged;
    };

    // Reads every plugin of `loadOrder` (masters first) into a SpellStore
    SpellStore ReadSpellStore(const std::vector<PluginFile>& loadOrder, NameTable& names);
}
