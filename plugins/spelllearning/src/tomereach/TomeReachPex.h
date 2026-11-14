#pragma once

// =============================================================================
// TOME REACH - COMPILED SCRIPTS (distribution model)
// =============================================================================
//
// Which of a script's properties reach AddItem (a reward, or a merchant restock)
// and which reach AddForm / AddForms / AddToList (a leveled list filled at run
// time): a disassembler for compiled Papyrus (.pex, big-endian) and a data flow
// from each property's auto variable through temporaries, casts, array elements
// and the script's own functions into those calls. A script that only names a
// form (a HasPerk check, a RemoveItem) hands nothing out. Port of the author's
// Python (scripts/pexdis.py, scripts/spellmap/core.py granted_properties), down
// to its quirks: arguments are compared as the disassembly writes them. Pure
// C++, shared by the plugin and tools/tome-reach-test.

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace TomeReach
{
    struct PexInstruction
    {
        std::uint8_t op = 0;
        std::vector<std::string> args;   // lower case, as the disassembly's text splits them
    };

    struct PexFunction
    {
        std::string name;                // lower case; property accessors are "<property>.get" / ".set"
        std::vector<std::string> params; // lower case
        std::vector<PexInstruction> code;   // instructions with arguments only
    };

    // Every function of every object and state, in file order; false when the file is damaged
    bool DisassemblePex(const std::vector<unsigned char>& data, std::vector<PexFunction>& out);

    struct ScriptGrants
    {
        std::unordered_set<std::string> toList;   // properties (lower case) that reach AddForm / AddForms / AddToList
        std::unordered_set<std::string> toItem;   // ... that reach AddItem
    };

    // Empty when the script cannot be read
    ScriptGrants AnalyseScript(const std::vector<unsigned char>& data);
}
