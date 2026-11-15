#pragma once

// =============================================================================
// TOME REACH - ATTACHED SCRIPTS (distribution model)
// =============================================================================
//
// The scripts records carry (VMAD) and the forms their properties hold, and what
// those scripts do with them (TomeReachPex.h): a property that reaches AddItem
// hands its items out (a reward; a restock when the script also holds a merchant
// chest), one that reaches AddForm puts them into the script's own leveled list
// properties. Port of the author's Python (scripts/spellmap/core.py parse_vmad,
// scripts/spellmap/reach.py Graph._scripts). Pure C++, shared by the plugin and
// tools/tome-reach-test.

#include "tomereach/TomeReachData.h"
#include "tomereach/TomeReachPex.h"
#include "tomereach/TomeReachPlugin.h"
#include "tomereach/TomeReachReader.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace TomeReach
{
    // The scripts of a VMAD subrecord with their form-valued properties; empty when it is damaged
    std::vector<AttachedScript> ParseVmad(const unsigned char* data, std::size_t size, const Resolver& resolve);

    // What each script grants, read once per script (loose, else from an archive)
    class ScriptAnalysis
    {
    public:
        explicit ScriptAnalysis(const DataFiles& data) : m_data(data) {}

        const ScriptGrants& Of(const std::string& script);

        [[nodiscard]] std::size_t Read() const { return m_read; }
        [[nodiscard]] std::size_t Missing() const { return m_missing; }

    private:
        const DataFiles& m_data;
        std::unordered_map<std::string, ScriptGrants> m_grants;
        std::size_t m_read = 0;
        std::size_t m_missing = 0;
    };
}
