// sr-reclassify-check - offline check of the Spell Research reclassify decisions
// (plugins/spelllearning/include/librarian/SpellResearchReclassify.h).
//
//   sr-reclassify-check --self-test
//
// A spell SR files as its record says stays; a changed tier or school drops every
// wrong script entry and joins the right list; a wrong entry declared in a plugin
// record locks the spell (nothing changes on either axis); a spell in no tier list
// is HoM's own registration's business, not this; no record school or tier, no change.
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "librarian/SpellResearchReclassify.h"

#include <cstdio>
#include <string_view>

namespace
{
    namespace R = Librarian::SpellResearch::Reclassify;

    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    // Index names, as SR orders its lists
    constexpr std::size_t kNovice = 0, kApprentice = 1, kAdept = 2, kExpert = 3;
    constexpr std::size_t kAlteration = 0, kConjuration = 1, kDestruction = 2, kRestoration = 4;

    int g_failures = 0;
    int g_checks = 0;

    void Check(bool ok, const char* what)
    {
        ++g_checks;
        if (!ok) {
            ++g_failures;
            std::printf("FAIL %s\n", what);
        }
    }

    R::Spell Make(R::Entries a_tiers, R::Entries a_schools, std::optional<std::size_t> a_tier, std::optional<std::size_t> a_school)
    {
        return R::Spell{ "Skyrim.esm|0x12FCD", std::move(a_tiers), std::move(a_schools), a_tier, a_school };
    }

    int SelfTest()
    {
        // As SR filed it: nothing to do
        auto same = R::Decide(Make({ { kNovice }, {} }, { { kDestruction }, {} }, kNovice, kDestruction));
        Check(same.outcome == R::Outcome::kSame && !same.tier.Changes() && !same.school.Changes(), "filed as the record says: same");

        // Requiem moved it up a tier: leave Novice, join Apprentice; school untouched
        auto tierUp = R::Decide(Make({ { kNovice }, {} }, { { kDestruction }, {} }, kApprentice, kDestruction));
        Check(tierUp.outcome == R::Outcome::kMove && tierUp.tier.drop == std::vector<std::size_t>{ kNovice }
                  && tierUp.tier.add == kApprentice && !tierUp.school.Changes(),
            "tier changed: drop the old tier, join the new one, school left");
        Check(tierUp.fromTier == kNovice && tierUp.fromSchool == kDestruction, "the decision names SR's reading before");

        // School changed (Restoration -> Alteration): the old school must go or first-match keeps... the new one first
        auto school = R::Decide(Make({ { kAdept }, {} }, { { kRestoration }, {} }, kAdept, kAlteration));
        Check(school.outcome == R::Outcome::kMove && school.school.drop == std::vector<std::size_t>{ kRestoration }
                  && school.school.add == kAlteration && !school.tier.Changes(),
            "school changed: drop the old school, join the new one");

        // In two tier lists (a patch added it again): every wrong one goes, the right one is kept, nothing added
        auto twice = R::Decide(Make({ { kNovice, kExpert }, {} }, { { kConjuration }, {} }, kExpert, kConjuration));
        Check(twice.outcome == R::Outcome::kMove && twice.tier.drop == std::vector<std::size_t>{ kNovice } && !twice.tier.add,
            "in the right and a wrong tier: the wrong one goes, no add");
        Check(R::FirstMatch({ { kNovice, kExpert }, {} }) == kNovice, "SR reads the first tier list that has it (Novice)");

        // A wrong entry in a plugin record: cannot leave it, so nothing changes on either axis
        auto locked = R::Decide(Make({ {}, { kNovice } }, { { kDestruction }, {} }, kApprentice, kAlteration));
        Check(locked.outcome == R::Outcome::kLocked && !locked.tier.Changes() && !locked.school.Changes(),
            "a wrong record entry: locked, no half change");
        // A record entry that is already right is fine
        auto recordRight = R::Decide(Make({ { kNovice }, { kApprentice } }, { {}, { kDestruction } }, kApprentice, kDestruction));
        Check(recordRight.outcome == R::Outcome::kMove && recordRight.tier.drop == std::vector<std::size_t>{ kNovice }
                  && !recordRight.tier.add && !recordRight.school.Changes(),
            "the right tier in a record, a wrong script one: the script one goes");

        // Not in SR's tier lists at all: HoM's own registration, not this
        auto notIn = R::Decide(Make({}, { { kDestruction }, {} }, kNovice, kDestruction));
        Check(notIn.outcome == R::Outcome::kNotInSR && !notIn.tier.Changes(), "in no tier list: notInSR, untouched");

        // No school or tier from the record: untouched
        auto unknown = R::Decide(Make({ { kNovice }, {} }, { { kDestruction }, {} }, std::nullopt, kDestruction));
        Check(unknown.outcome == R::Outcome::kUnknown && !unknown.tier.Changes() && !unknown.school.Changes(),
            "no record tier: unknown, untouched");

        // In no school list but in a tier list: join the record's school
        auto noSchool = R::Decide(Make({ { kAdept }, {} }, {}, kAdept, kConjuration));
        Check(noSchool.outcome == R::Outcome::kMove && noSchool.school.add == kConjuration && noSchool.school.drop.empty(),
            "in a tier but no school list: join the school");

        // Tome pair lists line up only with equal record and script counts
        Check(R::Aligned({ 2, 30, 2, 30 }) && !R::Aligned({ 2, 30, 2, 29 }) && !R::Aligned({ 3, 29, 2, 30 }),
            "tome pairs line up only with the same record and script counts");

        Check(std::string_view(R::OutcomeText(R::Outcome::kLocked)) == "locked", "outcome names for the report");

        std::printf("sr-reclassify-check: %d checks, %d failed\n", g_checks, g_failures);
        return g_failures == 0 ? kExitOk : kExitFailed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: sr-reclassify-check --self-test\n");
        return kExitUsage;
    }
    return SelfTest();
}
