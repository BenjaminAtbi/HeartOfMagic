#pragma once

// =============================================================================
// TOME REACH - COMPILED SCRIPTS (distribution model)
// =============================================================================
//
// Which of a script's properties reach AddItem or PlaceAtMe (a reward, or a
// merchant restock), which reach PlaceAtMe / PlaceActorAtMe (an NPC spawned),
// and which pairs of properties meet in one `List.AddForm(Item)` call (AddForm,
// AddForms, AddToList: a list filled at run time): a disassembler for compiled
// Papyrus (.pex, big-endian), a data flow from each property's auto variable
// through temporaries, casts, array elements and the script's own functions into
// those calls, and, per call, where its receiver and argument come from. A
// script that only names a form (a HasPerk check, a RemoveItem) hands nothing
// out. Port of the author's Python (scripts/pexdis.py, scripts/spellmap/core.py
// granted_properties, addform_pairs), down to its quirks: arguments are
// compared as the disassembly writes them. Pure C++, shared by the plugin and
// tools/tome-reach-test.

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

    // One `List.AddForm(Item)` call whose receiver and argument both come from properties within one function.
    // element: the value is some member of the property (GetAt of a form list, an array element), whichever
    struct AddFormPair
    {
        std::string receiver;
        bool receiverElement = false;
        std::string argument;
        bool argumentElement = false;

        friend auto operator<=>(const AddFormPair&, const AddFormPair&) = default;
    };

    struct ScriptGrants
    {
        std::unordered_set<std::string> give;    // properties (lower case) that reach AddItem or PlaceAtMe
        std::unordered_set<std::string> spawn;   // ... that reach PlaceAtMe or PlaceActorAtMe
        std::vector<AddFormPair> pairs;          // sorted, unique
    };

    // Empty when the script cannot be read
    ScriptGrants AnalyseScript(const std::vector<unsigned char>& data);
}
