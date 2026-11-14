#pragma once

// =============================================================================
// TOME REACH - SPID AND SKYPATCHER CONFIGS (distribution model)
// =============================================================================
//
// The configs that hand items out at load time:
// - SPID Item / DeathItem lines (*_DISTR.ini): a root of their own, judged by
//   their filters (factions, races, keywords, NPCs, names), not by the NPCs they
//   match;
// - SkyPatcher leveled list lines (filterByLLs=<lists>:addToLLs=<forms>): the forms
//   join the lists.
// Forms are written as an editor id, "0xID~Plugin.esp" or "Plugin.esp|0xID". The
// configs are read before the plugins, so the spell store resolves only the editor
// ids they name (ConfigWants). Port of the author's Python (scripts/spellmap/core.py
// resolve_token, spid_items, skypatcher_ll_adds). Pure C++, shared by the plugin and
// tools/tome-reach-test.

#include "tomereach/TomeReachData.h"
#include "tomereach/TomeReachReader.h"

#include <string>
#include <string_view>
#include <vector>

namespace TomeReach
{
    // A config's form: a plugin's form (key) or an editor id (lower case)
    struct FormToken
    {
        Key key = 0;
        std::string editorId;
    };

    FormToken ParseFormToken(std::string_view text, NameTable& names);

    struct SpidFilterPart
    {
        bool form = false;    // the form field; else the string field (editor ids or names)
        FormToken token;      // form parts
        std::string text;     // as written (string parts compared lower case)
    };

    struct RawSpidLine
    {
        std::vector<FormToken> forms;              // one root per form
        bool deathItem = false;
        std::string chance;
        std::string source;                        // the config's data path
        std::vector<SpidFilterPart> filters;       // included (not "-") filter parts
        std::vector<FormToken> formParts;          // every form filter part, excluded ones too
    };

    struct RawListAdd
    {
        std::vector<FormToken> forms;
        std::vector<FormToken> lists;
    };

    struct DistributionConfigs
    {
        std::vector<RawSpidLine> spid;
        std::vector<RawListAdd> listAdds;
        std::size_t files = 0;

        [[nodiscard]] ConfigWants Wants() const;
    };

    DistributionConfigs ReadDistributionConfigs(const DataFiles& data, NameTable& names);

    enum class FilterKind : std::uint8_t { kFaction, kRace, kKeyword, kNpc, kString, kOther };

    struct SpidLine
    {
        Key form = 0;
        bool deathItem = false;
        bool judged = false;                 // every form filter is a faction, race, keyword or NPC
        std::vector<FilterKind> filters;
        std::string chance;
        std::string source;
    };

    struct ListAdd
    {
        Key form = 0;
        std::vector<Key> lists;
    };

    struct Distribution
    {
        std::vector<SpidLine> spid;          // one per form of a line
        std::vector<ListAdd> listAdds;
    };

    // Resolves the configs' forms in the store; string filters naming an NPC (its editor id
    // or name, and no keyword of an NPC or race) count as NPC filters
    Distribution ResolveDistribution(const DistributionConfigs& configs, const SpellStore& store, const DataFiles& data,
        const NameTable& names);

    [[nodiscard]] Key ResolveToken(const FormToken& token, const SpellStore& store);

    // A SPID line's class and subtype (scripts/spellmap/distribution.py spid_route)
    struct SpidClass
    {
        std::string_view cls;
        std::string_view subtype;
    };
    [[nodiscard]] SpidClass ClassOf(const SpidLine& line);
}
