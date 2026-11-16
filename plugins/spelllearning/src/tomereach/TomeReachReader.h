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

#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
        kFormList,    // FLST
    };

    using RecordType = std::array<char, 4>;

    struct Npc
    {
        std::vector<Key> items;      // CNTO
        Key deathItem = 0;           // INAM
        Key outfit = 0;              // DOFT
        Key templ = 0;               // TPLT
        std::vector<Key> factions;   // SNAM
        std::uint32_t flags = 0;     // ACBS flags (0x20: unique)
        std::vector<Key> keywords;   // KWDA
        Key race = 0;                // RNAM
        std::string name;            // FULL, when it is one of the configs' strings (ConfigWants)
        std::uint32_t nameId = 0;    // FULL of a localized plugin: the string's id ...
        std::uint32_t namePlugin = 0;   // ... in this plugin's string table
    };

    // A script attached to a record (VMAD), with its properties that hold forms
    struct ScriptProperty
    {
        std::string name;            // lower case
        std::vector<Key> forms;      // object values, arrays flattened
    };

    struct AttachedScript
    {
        std::string name;            // lower case
        std::vector<ScriptProperty> properties;
    };

    struct ScriptHolder
    {
        RecordType type{};
        std::string editorId;
        std::vector<AttachedScript> scripts;
    };

    // What the configs name, so the store resolves only those editor ids (TomeReachDistr.h)
    struct ConfigWants
    {
        std::unordered_set<std::string> editorIds;   // lower case
        std::unordered_set<Key> keys;
        std::unordered_set<std::string> strings;     // SPID string filters, lower case: NPC names worth keeping
    };

    struct Recipe
    {
        Key created = 0;                  // CNAM
        Key bench = 0;                    // BNAM
        std::vector<Key> components;      // CNTO
        std::uint32_t plugin = 0;         // the winning version's plugin: resolves the conditions' forms
        std::vector<std::vector<unsigned char>> conditions;   // CTDA, as written
    };

    // A leveled item list's chance of nothing (LVLD) and the global that sets it in game (LVLG). The list is a
    // gated list when the chance is 100: it gives nothing until a script lowers the global (TomeReachSegments)
    struct GatedList
    {
        std::uint8_t chanceNone = 0;
        Key global = 0;
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
        std::unordered_map<Key, GatedList> gatedLists;      // LVLI whose chance of nothing a global holds (LVLG)
        std::unordered_map<Key, std::string> globalIds;     // GLOB editor ids
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
        std::unordered_map<Key, std::vector<Key>> formLists;   // FLST LNAM
        std::unordered_map<Key, ScriptHolder> holders;      // records of the script holder types with scripts
        std::unordered_map<Key, std::vector<Key>> raceKeywords;   // RACE KWDA
        std::unordered_map<Key, std::string> keywordIds;    // KYWD editor ids
        // The configs' forms: editor id -> the first record (in load order) carrying it, and
        // the record type of every form they name
        std::unordered_map<std::string, Key> tokenEditorIds;
        std::unordered_map<Key, RecordType> tokenTypes;
        std::vector<std::string> unreadable;
        std::vector<std::string> damaged;
    };

    // Reads every plugin of `loadOrder` (masters first) into a SpellStore; `wants`: the
    // configs' forms to resolve (none when null)
    // `tomeSpells`, when given (in game): the books the game pairs with a spell (SpellTomes) are the tomes,
    // each teaching its pair's spell whatever its DATA says (a book that teaches nothing, Spell Research
    // Synthesizer's, or one a DLL rebinds); a book with no pair is no tome. Null (the harness): DATA decides.
    SpellStore ReadSpellStore(const std::vector<PluginFile>& loadOrder, NameTable& names, const ConfigWants* wants = nullptr,
        const std::unordered_map<Key, Key>* tomeSpells = nullptr);
}
