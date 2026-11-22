#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// Spell Research reclassify - SR's school and tier for a spell it already knows,
// judged again from the spell's record (opt-in, spellResearch.reclassify)
//
// SR's Import files vanilla and DLC spells into its school and tier lists with
// the school and level its import script carries, so an overhaul that changes a
// vanilla spell's school or level (Requiem) is not seen. SR reads a spell's school
// and tier as the FIRST list that has it (_SRU_Utility GetSpellSkill: Alteration
// first; GetSpellRank: Novice first), so a spell must leave the wrong list, not
// only join the right one. Entries a plugin declares in the record cannot be taken
// out at runtime: a spell with a wrong record entry is left as it is (locked).
//
// Pure: the game side (LibrarianSpellResearchPatch.cpp) and tools/sr-reclassify-check
// make the same decisions from the same inputs.
// =============================================================================

namespace Librarian::SpellResearch::Reclassify
{
    // SR's school lists in GetSpellSkill's order (the tiers are kTiers, SpellResearch.h)
    inline constexpr std::array<std::string_view, 5> kSchools{ "alteration", "conjuration", "destruction", "illusion",
        "restoration" };

    // Where a spell is on one axis (tier or school): list indexes, split by how the entry got there
    struct Entries
    {
        std::vector<std::size_t> script;  // added by a script (SR's Import, HoM): removable
        std::vector<std::size_t> record;  // declared in a plugin record: not removable at runtime
    };

    // SR's reading of one axis: the lowest index that has the spell (first match), or nothing
    inline std::optional<std::size_t> FirstMatch(const Entries& a_entries)
    {
        std::optional<std::size_t> first;
        for (const auto* side : { &a_entries.script, &a_entries.record }) {
            for (const std::size_t index : *side) {
                if (!first || index < *first) first = index;
            }
        }
        return first;
    }

    // After a move: the spell is in the wanted list and in no other, whichever way each entry got there
    inline bool OnlyIn(const Entries& a_entries, std::size_t a_want)
    {
        const auto right = [a_want](std::size_t index) { return index == a_want; };
        return (!a_entries.script.empty() || !a_entries.record.empty()) && std::ranges::all_of(a_entries.script, right)
            && std::ranges::all_of(a_entries.record, right);
    }

    struct AxisChange
    {
        bool locked = false;              // a wrong entry is in a record: nothing can be done on this axis
        std::vector<std::size_t> drop;    // script entries to remove
        std::optional<std::size_t> add;   // the list to join (not already in it)
        bool Changes() const { return !drop.empty() || add.has_value(); }
    };

    // One axis: the spell ends in the wanted list only
    inline AxisChange PlanAxis(const Entries& a_entries, std::size_t a_want)
    {
        AxisChange change;
        const auto wrong = [a_want](std::size_t index) { return index != a_want; };
        if (std::ranges::any_of(a_entries.record, wrong)) {
            change.locked = true;
            return change;
        }
        for (const std::size_t index : a_entries.script) {
            if (wrong(index) && std::ranges::find(change.drop, index) == change.drop.end()) change.drop.push_back(index);
        }
        const bool has = std::ranges::find(a_entries.script, a_want) != a_entries.script.end()
            || std::ranges::find(a_entries.record, a_want) != a_entries.record.end();
        if (!has) change.add = a_want;
        return change;
    }

    enum class Outcome
    {
        kSame,     // SR already files it as its record says
        kMove,     // tier and/or school lists change
        kLocked,   // a wrong entry sits in a plugin record: left as it is, reported
        kUnknown,  // no school or tier from the record (catalog): left as it is
        kNotInSR,  // in no tier list: HoM's own registration handles it, not this
    };

    struct Spell
    {
        std::string id;                         // persistent id, for the report
        Entries tiers;                          // kTiers indexes
        Entries schools;                        // kSchools indexes
        std::optional<std::size_t> wantTier;    // from the record (catalog tier), if SR can file it
        std::optional<std::size_t> wantSchool;  // from the record (catalog school)
    };

    struct Decision
    {
        Outcome outcome = Outcome::kSame;
        AxisChange tier;
        AxisChange school;
        std::optional<std::size_t> fromTier;    // SR's reading before (first match)
        std::optional<std::size_t> fromSchool;
    };

    // Both axes or neither: a spell is moved only when every wrong entry can go, so SR never
    // reads half a change (the new tier with the old school still first, or the other way)
    inline Decision Decide(const Spell& a_spell)
    {
        Decision decision;
        decision.fromTier = FirstMatch(a_spell.tiers);
        decision.fromSchool = FirstMatch(a_spell.schools);
        if (!decision.fromTier) {
            decision.outcome = Outcome::kNotInSR;
            return decision;
        }
        if (!a_spell.wantTier || !a_spell.wantSchool) {
            decision.outcome = Outcome::kUnknown;
            return decision;
        }
        decision.tier = PlanAxis(a_spell.tiers, *a_spell.wantTier);
        decision.school = PlanAxis(a_spell.schools, *a_spell.wantSchool);
        if (decision.tier.locked || decision.school.locked) {
            decision.outcome = Outcome::kLocked;
            decision.tier = {};
            decision.school = {};
            return decision;
        }
        decision.outcome = (decision.tier.Changes() || decision.school.Changes()) ? Outcome::kMove : Outcome::kSame;
        return decision;
    }

    inline const char* OutcomeText(Outcome a_outcome)
    {
        switch (a_outcome) {
        case Outcome::kSame: return "same";
        case Outcome::kMove: return "move";
        case Outcome::kLocked: return "locked";
        case Outcome::kUnknown: return "unknown";
        case Outcome::kNotInSR: return "notInSR";
        }
        return "?";
    }
}
