#include "librarian/SpellResearchPatch.h"
#include "AsciiText.h"
#include "FormIdText.h"
#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"
#include "librarian/LoadOrderSource.h"
#include "librarian/PerkAdapters.h"
#include "librarian/SpellResearch.h"
#include "librarian/SpellResearchPlayerFilter.h"
#include "librarian/SpellResearchReclassify.h"
#include "EncodingUtils.h"
#include "librarian/PerkAdapterPatch.h"
#include <filesystem>
#include "HudNotify.h"
#include "ThreadUtils.h"
#include "JsonText.h"
#include "PathText.h"
#include "SpellScanner.h"
#include "StepTimes.h"
#include "SpellTomes.h"
#include "tomereach/TomeReach.h"
#include "ProgressionNotify.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

// =============================================================================
// LibrarianSpellResearchPatch - registering catalog spells with Spell Research
//
// See SpellResearchPatch.h for when this runs and SpellResearch.h for what a
// registration holds.
// =============================================================================

namespace Librarian::SpellResearchPatch
{
    using namespace Librarian::Detail;
    namespace Adapters = Librarian::Adapters;
    namespace SR = Librarian::SpellResearch;

    namespace
    {
        constexpr const char* SR_PLUGIN = "SpellResearch.esp";
        constexpr const char* RULES_FILE = "compat/spellresearch.json";
        constexpr const char* REPORT_FILE = "spell_research_report.json";
        constexpr const char* PARITY_FILE = "spell_research_parity.json";
        constexpr const char* CONFIG_SECTION = "spellResearch";
        // SendModEvent("SR_ImportFinished"): an SR build that imports with no message box says it is done
        constexpr std::string_view IMPORT_FINISHED_EVENT = "SR_ImportFinished";

        bool g_installed = false;
        SR::Lists g_lists;

        // Entries in the five tier lists after the last run, for the save
        // loaded now. Same count at the next message box: no Import ran since.
        std::size_t g_lastSeen = 0;

        // Spells whose tier list would not take them: left out of the check
        // for missing spells, or every load would plan them again.
        std::unordered_set<RE::FormID> g_failed;

        bool g_waitingForPass = false;   // a Reconcile is queued for after the tome reach pass
        std::uint64_t g_gameSession = 0;  // +1 at every save load and new game: a queued Reconcile of another is dropped

        // What the last run found (StatusJson): written on the game thread, read
        // when the panel opens
        std::mutex g_statusLock;
        json g_status;

        void SetStatus(const char* state, std::string_view reason, std::size_t srSpells = 0, std::size_t added = 0)
        {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            char clock[8] = {};
            std::strftime(clock, sizeof(clock), "%H:%M", &local);
            {
                std::lock_guard lock(g_statusLock);
                g_status = { { "state", state }, { "reason", std::string(reason) }, { "srSpells", srSpells },
                             { "added", added }, { "time", clock } };
            }
            // The panel's line, if it is open (a scan from the panel ends here)
            ProgressionNotify::SpellResearchStatus();
        }

        RE::BGSListForm* ResolveList(const std::string& persistentId)
        {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(persistentId);
            return formId ? RE::TESForm::LookupByID<RE::BGSListForm>(formId) : nullptr;
        }

        float GlobalValue(const std::string& persistentId)
        {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(persistentId);
            auto* global = formId ? RE::TESForm::LookupByID<RE::TESGlobal>(formId) : nullptr;
            return global ? global->value : 0.0f;
        }

        // The plugin's entries and the ones scripts added, as Papyrus GetSize counts them
        std::size_t Count(const RE::BGSListForm* list)
        {
            if (!list) return 0;
            return list->forms.size() + (list->scriptAddedTempForms ? list->scriptAddedTempForms->size() : 0);
        }

        // Every spell in any of SR's tier lists, and how many entries they hold
        struct Registered
        {
            std::unordered_set<RE::FormID> spells;
            std::size_t entries = 0;
        };

        // Entries in the tier lists: what tells an Import or a load apart from
        // any other message box, before anything costlier is read
        std::size_t CountRegistered()
        {
            std::size_t entries = 0;
            for (const auto& id : g_lists.allSpells) entries += Count(ResolveList(id));
            return entries;
        }

        Registered ReadRegistered()
        {
            Registered result;
            for (const auto& id : g_lists.allSpells) {
                const auto* list = ResolveList(id);
                if (!list) continue;
                result.entries += Count(list);
                list->ForEachForm([&](RE::TESForm* form) {
                    result.spells.insert(form->GetFormID());
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            return result;
        }

        bool HasSchoolAndTier(const json& entry)
        {
            const auto text = [&](const char* key) {
                const auto found = entry.find(key);
                return (found != entry.end() && found->is_string()) ? found->get<std::string>() : std::string();
            };
            return g_lists.school.contains(text("school"))
                && std::ranges::find(SR::kTiers, text("tier")) != SR::kTiers.end();
        }

        // Tome spells the tree leaves out and SR is not given, by why (MissingSpells)
        struct Skipped
        {
            std::size_t voiceSlot = 0;
            std::size_t gateOut = 0;      // the spell gate's "out" (free, a summon that stays, a rule)
            std::size_t duplicate = 0;
            std::size_t unreachable = 0;
            std::size_t playerLeftOut = 0;   // the player's spell blacklist or a plugin turned off
        };

        // Another plugin's copy of a spell (duplicateOf in the scan), or the kept
        // record of a group SR already holds a member of: each spell is in SR once
        bool DuplicateLeftOut(RE::FormID formId, const Registered& registered)
        {
            const auto group = SpellScanner::DuplicateGroupOf(formId);
            if (group.empty()) return false;
            if (group.front() != formId) return true;
            return std::ranges::any_of(group, [&](RE::FormID member) { return registered.spells.contains(member); });
        }

        // The catalog spells to register: a tome teaches them (the catalog also
        // holds NPC, quest and creature spells, which SR must not hand out), and
        // the rest exactly as the panel's builder takes spells into the tree
        // (2026-10-05): whose tome something hands out (not tomeUnreachable), not
        // in the voice slot, not left out by the spell gate (JudgeSpell), not another plugin's copy
        // (duplicateOf), not blacklisted and not from a plugin turned off in the
        // whitelist. SR can file them by school and tier, and no tier list has them yet.
        json MissingSpells(const json& catalogSpells, const Registered& registered,
            const std::unordered_map<RE::FormID, RE::TESObjectBOOK*>& tomes,
            Skipped& skipped)
        {
            std::unordered_set<RE::FormID> unreachable;
            TomeReach::UnreachableSpells(unreachable);   // Reconcile waits for a running pass; empty when it failed
            const PlayerFilter playerFilter = ReadPlayerFilter();
            const auto gateKept = SpellScanner::ReadGateKept();
            json missing = json::object();
            for (const auto& [id, entry] : catalogSpells.items()) {
                if (!HasSchoolAndTier(entry)) continue;
                const RE::FormID formId = SpellScanner::ResolvePersistentFormId(id);
                if (!formId || !tomes.contains(formId) || registered.spells.contains(formId) || g_failed.contains(formId)) continue;
                auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
                if (!spell) continue;
                if (SpellScanner::IsEquippedInVoiceSlot(spell)) {
                    ++skipped.voiceSlot;
                } else if (unreachable.contains(formId)) {
                    ++skipped.unreachable;
                } else if (PlayerLeftOut(id, formId, playerFilter)) {
                    ++skipped.playerLeftOut;
                } else if (SpellScanner::GateLeavesOut(spell, gateKept)) {
                    ++skipped.gateOut;
                } else if (DuplicateLeftOut(formId, registered)) {
                    ++skipped.duplicate;
                } else {
                    missing[id] = entry;
                }
            }
            return missing;
        }

        void WriteReport(const json& report)
        {
            const auto path = DataPath(REPORT_FILE);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                logger::warn("SpellResearch: could not write '{}'", PathText::Utf8(path));
                return;
            }
            file << JsonText::Dump(report, 2);
        }

        // Debug installs (debug.json, as for the scan dump): every planned registration,
        // sorted, so two runs on one load order and save compare spell for spell
        void WriteParity(std::string_view reason, std::size_t missing, const SR::Registrations& registrations)
        {
            if (!SpellScanner::DebugScanDumpEnabled()) return;
            std::vector<std::string> lines;
            for (const auto& registration : registrations.spells) {
                std::string line = registration.spellId + " tier " + std::to_string(registration.tier) + " tome " + registration.tome;
                for (const auto& list : registration.lists) line += " " + list;
                lines.push_back(std::move(line));
            }
            std::sort(lines.begin(), lines.end());
            const json parity = { { "reason", std::string(reason) }, { "missing", missing },
                { "noSchoolOrTier", registrations.noSchoolOrTier }, { "registrations", lines } };
            const auto path = DataPath(PARITY_FILE);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (file.is_open()) file << JsonText::Dump(parity, 2);
        }

        struct Totals
        {
            std::size_t spells = 0;
            std::size_t entries = 0;
            std::size_t tomes = 0;
            std::size_t failed = 0;
            std::size_t tomeTiersSkipped = 0;
        };

        // Adds the registrations (spells no tier list has yet), SR's order.
        Totals Register(const SR::Registrations& registrations, json& report)
        {
            Totals totals;
            std::unordered_map<std::string, RE::BGSListForm*> resolved;
            const auto listOf = [&](const std::string& id) {
                const auto found = resolved.find(id);
                if (found != resolved.end()) return found->second;
                return resolved.emplace(id, ResolveList(id)).first->second;
            };

            // A tier's tome lists are paired by index: add to them only while
            // they line up - the same count, and as many record entries ahead
            // of the script-added ones - or SR would hand out the wrong book.
            std::array<bool, SR::kTiers.size()> tomesPaired{};
            for (std::size_t t = 0; t < SR::kTiers.size(); ++t) {
                const auto* bookList = listOf(g_lists.tomes[t]);
                const auto* spellList = listOf(g_lists.tomeSpells[t]);
                const std::size_t books = Count(bookList);
                const std::size_t taught = Count(spellList);
                tomesPaired[t] = bookList && spellList && books == taught
                    && bookList->forms.size() == spellList->forms.size();
                if (!tomesPaired[t]) {
                    ++totals.tomeTiersSkipped;
                    report["tomeTiersSkipped"].push_back({ { "tier", std::string(SR::kTiers[t]) }, { "tomes", books }, { "spells", taught } });
                }
            }

            bool firstAdd = true;
            json perList = json::object();
            for (const auto& registration : registrations.spells) {
                const RE::FormID formId = SpellScanner::ResolvePersistentFormId(registration.spellId);
                auto* spell = formId ? RE::TESForm::LookupByID<RE::SpellItem>(formId) : nullptr;
                if (!spell) continue;

                for (const auto& id : registration.lists) {
                    auto* list = listOf(id);
                    if (!list || list->HasForm(spell)) continue;
                    if (firstAdd) {
                        // AddForm must take the script path: Revert() clears only that
                        const auto before = list->scriptAddedFormCount;
                        list->AddForm(spell);
                        logger::info("SpellResearch: first entry - script-added count {} -> {} (forms {})",
                            before, list->scriptAddedFormCount, list->forms.size());
                        firstAdd = false;
                    } else {
                        list->AddForm(spell);
                    }
                    ++totals.entries;
                    const std::string name = SR::ArchetypeOf(g_lists, id);
                    perList[name.empty() ? id : name] = perList.value(name.empty() ? id : name, 0) + 1;
                }

                auto* tierList = listOf(g_lists.allSpells[registration.tier]);
                if (!tierList || !tierList->HasForm(spell)) {
                    g_failed.insert(formId);
                    ++totals.failed;
                    continue;
                }
                ++totals.spells;

                if (registration.tome.empty() || !tomesPaired[registration.tier]) continue;
                auto* tome = RE::TESForm::LookupByID<RE::TESObjectBOOK>(SpellScanner::ResolvePersistentFormId(registration.tome));
                if (!tome) continue;
                auto* books = listOf(g_lists.tomes[registration.tier]);
                auto* taught = listOf(g_lists.tomeSpells[registration.tier]);
                if (books->HasForm(tome) || taught->HasForm(spell)) continue;
                books->AddForm(tome);
                taught->AddForm(spell);
                if (Count(books) != Count(taught)) {
                    // One of the two did not take: stop this tier before the pairs drift further
                    tomesPaired[registration.tier] = false;
                    logger::warn("SpellResearch: {} tome lists out of step after {} - no more tomes there",
                        SR::kTiers[registration.tier], registration.spellId);
                    continue;
                }
                ++totals.tomes;
            }
            report["perList"] = std::move(perList);
            return totals;
        }

        // =====================================================================
        // Reclassify (opt-in, spellResearch.reclassify): SR's school and tier for the
        // vanilla and DLC spells its Import filed, judged again from the live records
        // (SpellResearchReclassify.h has the rules; GetSpellSchool and DetermineSpellTier,
        // as the scan and the tree use them).
        //
        // How a form list holds its entries (1.6.1170: AddForm 20913, RemoveAddedForm's
        // internal 20914, Save 20915, GetAt 20907; Codex disassembly 2026-11-20): `forms`
        // is the record's entries, then the script-added ones (spells, books: non-dynamic
        // forms); `scriptAddedTempForms` holds script-added dynamic (0xFF) forms;
        // `scriptAddedFormCount` counts every script-added entry in both arrays. Papyrus
        // indexes the temp array first, then `forms`. So a record entry is one in the first
        // forms.size() - (count - temp) slots, and nothing else.
        // =====================================================================

        namespace RC = Librarian::SpellResearch::Reclassify;
        constexpr const char* RECLASSIFY_KEY = "reclassify";
        constexpr const char* RECLASSIFY_FILE = "spell_research_reclassify.json";
        // The masters SR's own Import reads: other spells were filed by HoM (already as their
        // records say) or by a patch author's choice, which this leaves alone (user: "vanilla spells")
        constexpr std::array<std::string_view, 5> kImportMasters{ "Skyrim.esm", "Update.esm", "Dawnguard.esm",
            "HearthFires.esm", "Dragonborn.esm" };

        bool FromImportMaster(RE::SpellItem* spell)
        {
            const auto* file = spell ? spell->GetFile(0) : nullptr;
            if (!file) return false;
            const std::string_view name = file->GetFilename();
            return std::ranges::any_of(kImportMasters, [&](std::string_view master) { return AsciiText::EqualsIgnoreCase(name, master); });
        }

        struct ListShape
        {
            std::size_t records = 0;  // record entries at the front of `forms`
            std::size_t temp = 0;     // scriptAddedTempForms size
            std::size_t total = 0;    // what Papyrus GetSize counts
            bool sane = false;        // the counts fit together (another mod's direct edit can break them)
        };

        ListShape ShapeOf(const RE::BGSListForm* list)
        {
            ListShape shape;
            if (!list) return shape;
            const std::size_t forms = list->forms.size();
            shape.temp = list->scriptAddedTempForms ? list->scriptAddedTempForms->size() : 0;
            const std::size_t count = list->scriptAddedFormCount;
            shape.total = forms + shape.temp;
            shape.sane = count >= shape.temp && count - shape.temp <= forms;
            shape.records = shape.sane ? forms - (count - shape.temp) : forms;
            return shape;
        }

        enum class Where { kNone, kRecord, kScript };

        struct Located
        {
            Where where = Where::kNone;
            std::size_t index = 0;  // Papyrus index (GetAt)
        };

        Located Locate(const RE::BGSListForm* list, RE::FormID id)
        {
            if (!list) return {};
            const ListShape shape = ShapeOf(list);
            if (list->scriptAddedTempForms) {
                const auto& temp = *list->scriptAddedTempForms;
                for (std::uint32_t i = 0; i < temp.size(); ++i) {
                    if (temp[i] == id) return { Where::kScript, i };
                }
            }
            for (std::uint32_t j = 0; j < list->forms.size(); ++j) {
                const auto* form = list->forms[j];
                if (form && form->GetFormID() == id) {
                    return { (shape.sane && j >= shape.records) ? Where::kScript : Where::kRecord, shape.temp + j };
                }
            }
            return {};
        }

        RE::TESForm* At(const RE::BGSListForm* list, std::size_t a_index)
        {
            if (!list) return nullptr;
            const std::size_t temp = list->scriptAddedTempForms ? list->scriptAddedTempForms->size() : 0;
            if (a_index < temp) return RE::TESForm::LookupByID((*list->scriptAddedTempForms)[static_cast<std::uint32_t>(a_index)]);
            const std::size_t j = a_index - temp;
            return j < list->forms.size() ? list->forms[static_cast<std::uint32_t>(j)] : nullptr;
        }

        // What Papyrus FormList.RemoveAddedForm does (20914): the temp array first, then the
        // script-added part of `forms`; one fewer script-added entry; the list marked changed
        // so the save writes it. A record entry is never touched. False when nothing went.
        bool RemoveAdded(RE::BGSListForm* list, RE::FormID id)
        {
            if (!list) return false;
            const ListShape shape = ShapeOf(list);
            if (!shape.sane || list->scriptAddedFormCount == 0) return false;
            bool removed = false;
            if (list->scriptAddedTempForms) {
                auto& temp = *list->scriptAddedTempForms;
                const auto found = std::find(temp.begin(), temp.end(), id);
                if (found != temp.end()) {
                    temp.erase(found);
                    removed = true;
                }
            }
            if (!removed) {
                for (auto it = list->forms.begin() + static_cast<std::ptrdiff_t>(shape.records); it != list->forms.end(); ++it) {
                    if (*it && (*it)->GetFormID() == id) {
                        list->forms.erase(it);
                        removed = true;
                        break;
                    }
                }
            }
            if (!removed) return false;
            --list->scriptAddedFormCount;
            list->AddChange(RE::BGSListForm::ChangeFlags::kAddedForm);   // what 20914 marks after a removal
            return true;
        }

        // One script-added entry by its Papyrus index (the temp array, then forms): a tome pair is the
        // book and the spell at one index, and a list may hold the same book twice
        bool RemoveAddedAt(RE::BGSListForm* list, std::size_t a_index)
        {
            if (!list) return false;
            const ListShape shape = ShapeOf(list);
            if (!shape.sane || list->scriptAddedFormCount == 0) return false;
            if (a_index < shape.temp) {
                auto& temp = *list->scriptAddedTempForms;
                temp.erase(temp.begin() + static_cast<std::ptrdiff_t>(a_index));
            } else {
                const std::size_t j = a_index - shape.temp;
                if (j < shape.records || j >= list->forms.size()) return false;   // a record entry, or past the end
                list->forms.erase(list->forms.begin() + static_cast<std::ptrdiff_t>(j));
            }
            --list->scriptAddedFormCount;
            list->AddChange(RE::BGSListForm::ChangeFlags::kAddedForm);
            return true;
        }

        RC::Entries EntriesOf(const std::array<RE::BGSListForm*, 5>& lists, RE::FormID id)
        {
            RC::Entries entries;
            for (std::size_t i = 0; i < lists.size(); ++i) {
                const Located at = Locate(lists[i], id);
                if (at.where == Where::kRecord) entries.record.push_back(i);
                if (at.where == Where::kScript) entries.script.push_back(i);
            }
            return entries;
        }

        // DetermineSpellTier's two sources, kept apart: the half-cost perk, else the school effect's
        // minimum skill - but a minimum skill of 0 says nothing (vanilla Novice spells carry a Novice
        // perk), so then there is no record tier and the spell is left as SR files it
        std::optional<std::size_t> RecordTier(RE::SpellItem* spell, const char*& a_source)
        {
            std::string tier;
            a_source = "none";
            if (auto* perk = Librarian::PerkAdapters::OriginalCastingPerk(spell)) {
                tier = SpellScanner::GetSkillLevelFromPerk(perk);
                if (!tier.empty()) a_source = "perk";
            }
            if (tier.empty()) {
                const auto* effect = SpellScanner::GetSchoolEffect(spell);
                const std::uint32_t minimum = effect && effect->baseEffect ? effect->baseEffect->GetMinimumSkillLevel() : 0;
                if (minimum == 0) return std::nullopt;
                tier = SpellScanner::GetSkillLevelName(minimum);
                a_source = "minimumSkill";
            }
            const auto found = std::ranges::find(SR::kTiers, AsciiText::ToLower(tier));
            if (found == SR::kTiers.end()) return std::nullopt;
            return static_cast<std::size_t>(found - SR::kTiers.begin());
        }

        std::optional<std::size_t> RecordSchool(RE::SpellItem* spell)
        {
            switch (SpellScanner::GetSpellSchool(spell)) {
            case RE::ActorValue::kAlteration: return 0;
            case RE::ActorValue::kConjuration: return 1;
            case RE::ActorValue::kDestruction: return 2;
            case RE::ActorValue::kIllusion: return 3;
            case RE::ActorValue::kRestoration: return 4;
            default: return std::nullopt;
            }
        }

        std::string JoinNotes(const std::vector<std::string>& notes)
        {
            std::string text;
            for (const auto& note : notes) text += (text.empty() ? "" : "; ") + note;
            return text;
        }

        // Two pair lists line up when a spell's book sits at the spell's Papyrus index (SR: Find
        // in the spell list, GetAt in the book list): the same temp size and the same total
        bool PairsLineUp(const RE::BGSListForm* books, const RE::BGSListForm* spells)
        {
            const ListShape b = ShapeOf(books), s = ShapeOf(spells);
            return b.sane && s.sane && b.temp == s.temp && b.total == s.total;
        }

        // The spell's tome pairs: every tier but a_want gives its script pair up, and a_want gets one
        // when it has none (book first, as SR and Register add them). Planned before anything is
        // touched: when a_want cannot take the pair (its lists out of step, or the book already
        // there), every pair stays where it is. A record pair, lists out of step or a book not at the
        // spell's index keep that tier's pair. Several pairs elsewhere: one goes to a_want, the rest
        // (the same spell taught twice) are dropped.
        std::string MoveTomePairs(RE::SpellItem* spell, std::size_t a_want)
        {
            const RE::FormID id = spell->GetFormID();
            struct Pair
            {
                std::size_t tier;
                std::size_t index;   // the pair's Papyrus index in both lists
                RE::TESForm* book;
            };
            std::vector<Pair> movable;
            std::vector<std::string> notes;
            bool wantHasPair = false;
            for (std::size_t t = 0; t < SR::kTiers.size(); ++t) {
                const auto* books = ResolveList(g_lists.tomes[t]);
                const auto* spells = ResolveList(g_lists.tomeSpells[t]);
                const Located at = Locate(spells, id);
                if (at.where == Where::kNone) continue;
                if (t == a_want) {
                    wantHasPair = true;
                    continue;
                }
                const std::string tier(SR::kTiers[t]);
                if (at.where == Where::kRecord) {
                    notes.push_back(tier + ": record pair kept");
                    continue;
                }
                if (!PairsLineUp(books, spells)) {
                    notes.push_back(tier + ": pairs out of step, kept");
                    continue;
                }
                RE::TESForm* book = At(books, at.index);
                const Located bookAt = book ? Locate(books, book->GetFormID()) : Located{};
                if (!book || bookAt.where != Where::kScript || bookAt.index != at.index) {
                    notes.push_back(tier + ": book not a script entry at the spell's index, kept");
                    continue;
                }
                movable.push_back({ t, at.index, book });
            }
            if (movable.empty()) return notes.empty() ? std::string("no tome pair") : JoinNotes(notes);

            auto* wantBooks = ResolveList(g_lists.tomes[a_want]);
            auto* wantSpells = ResolveList(g_lists.tomeSpells[a_want]);
            if (!wantHasPair && (!PairsLineUp(wantBooks, wantSpells)
                                    || Locate(wantBooks, movable.front().book->GetFormID()).where != Where::kNone)) {
                notes.push_back(std::string(SR::kTiers[a_want]) + " cannot take the pair (out of step or the book already "
                                "there): every pair kept");
                return JoinNotes(notes);
            }
            for (const Pair& pair : movable) {
                auto* books = ResolveList(g_lists.tomes[pair.tier]);
                auto* spells = ResolveList(g_lists.tomeSpells[pair.tier]);
                const bool removed = RemoveAddedAt(books, pair.index) && RemoveAddedAt(spells, pair.index);
                notes.push_back(std::string(SR::kTiers[pair.tier]) + (removed && PairsLineUp(books, spells)
                                                                          ? ": pair removed" : ": removal FAILED, lists checked"));
            }
            if (!wantHasPair) {
                RE::TESForm* book = movable.front().book;
                wantBooks->AddForm(book);
                wantSpells->AddForm(spell);
                const Located b = Locate(wantBooks, book->GetFormID()), s = Locate(wantSpells, id);
                notes.push_back(std::string(SR::kTiers[a_want])
                                + (b.where != Where::kNone && s.where != Where::kNone && b.index == s.index ? ": pair added"
                                                                                                            : ": pair added OUT OF STEP"));
            }
            return JoinNotes(notes);
        }

        // spellResearch.enabled and .reclassify, read again only when config.json's write time changes:
        // Reconcile runs at every message box. Game thread only.
        bool ReclassifyOn()
        {
            static std::optional<std::filesystem::file_time_type> seen;
            static bool on = false;
            std::error_code error;
            const auto written = std::filesystem::last_write_time(DataPath("config.json"), error);
            if (error) return false;
            if (seen && *seen == written) return on;
            seen = written;
            on = ConfigSwitchOn(CONFIG_SECTION) && ConfigOptIn(CONFIG_SECTION, RECLASSIFY_KEY);
            return on;
        }

        void Reclassify(std::string_view reason)
        {
            if (!ReclassifyOn()) return;
            const auto started = std::chrono::steady_clock::now();
            std::array<RE::BGSListForm*, 5> tiers{}, schools{};
            for (std::size_t i = 0; i < 5; ++i) {
                tiers[i] = ResolveList(g_lists.allSpells[i]);
                schools[i] = ResolveList(g_lists.school.at(std::string(RC::kSchools[i])));
                if (!tiers[i] || !schools[i] || !ShapeOf(tiers[i]).sane || !ShapeOf(schools[i]).sane) {
                    logger::warn("SpellResearch: {} - reclassify skipped: SR's {} lists are missing or their counts do not fit "
                                 "(another mod edited them directly?)", reason, i < 5 ? "tier/school" : "");
                    return;
                }
            }
            const Registered registered = ReadRegistered();
            std::size_t moved = 0, partial = 0, locked = 0, unknown = 0, same = 0;
            json changes = json::array();
            json lockedList = json::array();
            for (const RE::FormID formId : registered.spells) {
                auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
                if (!FromImportMaster(spell)) continue;
                const std::string id = SpellScanner::GetPersistentFormId(formId);
                const char* tierSource = "none";
                const auto wantTier = RecordTier(spell, tierSource);
                const RC::Spell input{ id, EntriesOf(tiers, formId), EntriesOf(schools, formId), wantTier, RecordSchool(spell) };
                const std::string name = EncodingUtils::SanitizeToUTF8(spell->GetName());
                const RC::Decision decision = RC::Decide(input);
                switch (decision.outcome) {
                case RC::Outcome::kSame: ++same; continue;
                case RC::Outcome::kUnknown: ++unknown; continue;
                case RC::Outcome::kNotInSR: continue;
                case RC::Outcome::kLocked:
                    ++locked;
                    lockedList.push_back({ { "spell", id }, { "name", name } });
                    continue;
                case RC::Outcome::kMove: break;
                }
                // Join the right lists first, then leave the wrong ones: never a moment in none. A join
                // the list refused leaves the wrong entries too (the spell keeps SR's reading)
                if (decision.tier.add) tiers[*decision.tier.add]->AddForm(spell);
                if (decision.school.add) schools[*decision.school.add]->AddForm(spell);
                const bool joined = (!decision.tier.add || tiers[*decision.tier.add]->HasForm(spell))
                    && (!decision.school.add || schools[*decision.school.add]->HasForm(spell));
                if (joined) {
                    for (const std::size_t t : decision.tier.drop) RemoveAdded(tiers[t], formId);
                    for (const std::size_t s : decision.school.drop) RemoveAdded(schools[s], formId);
                }
                const std::string tome = !joined ? std::string("join refused, nothing moved")
                    : decision.tier.Changes() ? MoveTomePairs(spell, *input.wantTier) : std::string("tier unchanged");
                // What SR reads now: exactly the wanted tier and school, or a partial change to report
                const RC::Entries tiersAfter = EntriesOf(tiers, formId), schoolsAfter = EntriesOf(schools, formId);
                const bool done = RC::OnlyIn(tiersAfter, *input.wantTier) && RC::OnlyIn(schoolsAfter, *input.wantSchool);
                ++(done ? moved : partial);
                changes.push_back({ { "spell", id }, { "name", name }, { "result", done ? "moved" : "partial" },
                    { "tierSource", tierSource },
                    { "from", { { "tier", std::string(SR::kTiers[*decision.fromTier]) },
                                { "school", decision.fromSchool ? std::string(RC::kSchools[*decision.fromSchool]) : std::string() } } },
                    { "to", { { "tier", std::string(SR::kTiers[*input.wantTier]) }, { "school", std::string(RC::kSchools[*input.wantSchool]) } } },
                    { "tomePairs", tome } });
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            const json report = { { "reason", std::string(reason) }, { "moved", moved }, { "partial", partial }, { "same", same },
                { "locked", locked }, { "unknown", unknown }, { "elapsedMs", elapsed }, { "changes", changes },
                { "lockedSpells", lockedList },
                { "note", "With Spell Research Upgrade and Fixes, run its spell cache again: the cache keeps each spell's old "
                          "archetypes. Turning this off keeps the changes until SR's Import runs again." } };
            // A run that changed nothing (the next message box) leaves the last report standing
            if (moved + partial == 0) {
                // At info once per loaded game (the release log shows it ran), at debug after that
                static std::uint64_t loggedSession = ~std::uint64_t{ 0 };
                const bool first = loggedSession != g_gameSession;
                loggedSession = g_gameSession;
                const std::string line = std::format("SpellResearch: {} - reclassify: nothing to move ({} already right, {} "
                                                     "locked, {} without a record school or tier, {} ms)",
                    reason, same, locked, unknown, elapsed);
                if (first) logger::info("{}", line);
                else logger::debug("{}", line);
                return;
            }
            const auto path = DataPath(RECLASSIFY_FILE);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (file.is_open()) file << JsonText::Dump(report, 2);
            if (!file.good()) logger::warn("SpellResearch: could not write '{}'", PathText::Utf8(path));
            logger::info("SpellResearch: {} - reclassify: {} vanilla spells moved to the school and tier their records give, "
                         "{} partly ({}), {} already right, {} locked by a plugin's own list entry, {} without a record school "
                         "or tier, in {} ms", reason, moved, partial, RECLASSIFY_FILE, same, locked, unknown, elapsed);
        }

        void SafeReconcile(std::string_view reason);

        // One Reconcile once the tome reach pass is done. A newer pass that took over
        // before the task ran (an Import changed the tome pairs) is waited for in turn,
        // g_waitingForPass kept: SafeReconcile("import") queued no wait of its own while
        // it was set, so clearing it skipped the registrations until another event.
        // False when no thread could wait (RunAfterPass logged it).
        bool WaitForPassThenReconcile()
        {
            return TomeReach::RunAfterPass("SpellResearchReconcile", [session = g_gameSession](TomeReach::PassOutcome outcome) {
                if (session != g_gameSession) return;   // that game is gone (its own load reconciles)
                if (outcome == TomeReach::PassOutcome::kSuperseded) {
                    g_waitingForPass = WaitForPassThenReconcile();
                    if (!g_waitingForPass) logger::warn("SpellResearch: no wait for the newer tome reach pass - nothing registered");
                    return;
                }
                g_waitingForPass = false;
                if (RE::UI::GetSingleton()->IsMenuOpen(RE::MainMenu::MENU_NAME)) return;
                if (outcome != TomeReach::PassOutcome::kAnswered) {
                    logger::warn("SpellResearch: the tome reach pass failed or did not finish - nothing registered");
                    return;
                }
                SafeReconcile("tome reach done");
            });
        }

        void Reconcile(std::string_view reason)
        {
            if (!g_installed) return;

            // Only once an Import finished, never while one runs, and only when
            // the tier lists changed since the last run (an Import, a load)
            if (GlobalValue(g_lists.isSetup) != 1.0f) {
                SetStatus("notImported", reason);
                return;
            }
            if (GlobalValue(g_lists.importing) != 0.0f) return;
            // Opt-in, before the count check: a re-import that ends with the same count puts SR's
            // classes back, and the moves themselves keep the count
            try {
                Reclassify(reason);   // opt-in (ReclassifyOn); its failure must not cost this run's registrations
            } catch (const std::exception& e) {
                logger::error("SpellResearch: {} - reclassify failed - {}", reason, e.what());
            }
            const std::size_t entries = CountRegistered();
            if (entries == g_lastSeen) return;
            if (TomeReach::IsRunning()) {
                // Without its answer spells whose tomes nothing hands out would go in for good:
                // nothing now, g_lastSeen kept, one run once the pass is done
                if (!g_waitingForPass) g_waitingForPass = WaitForPassThenReconcile();
                return;
            }
            g_lastSeen = entries;
            if (!ConfigSwitchOn(CONFIG_SECTION)) {
                SetStatus("off", reason);
                return;
            }

            const auto started = std::chrono::steady_clock::now();
            StepTimes steps;
            json catalog;
            if (!LoadCatalog(catalog) || !catalog.is_object() || !catalog.contains("spells") || !catalog["spells"].is_object()) {
                logger::info("SpellResearch: {} - no catalog yet, a full scan makes one", reason);
                SetStatus("noCatalog", reason);
                return;
            }
            steps.Mark("catalog");
            const Registered registered = ReadRegistered();
            const auto tomes = SpellScanner::TomesBySpell();
            steps.Mark("registered and tomes");
            Skipped skipped;
            const json missing = MissingSpells(catalog["spells"], registered, tomes, skipped);
            steps.Mark("missing");
            if (skipped.voiceSlot || skipped.gateOut || skipped.duplicate || skipped.unreachable || skipped.playerLeftOut) {
                logger::info("SpellResearch: {} - left out as the tree leaves them: {} voice slot spells, "
                             "{} the spell gate leaves out, {} copies from another plugin, {} whose tomes nothing hands out, "
                             "{} blacklisted or from a plugin turned off", reason, skipped.voiceSlot,
                             skipped.gateOut, skipped.duplicate, skipped.unreachable, skipped.playerLeftOut);
            }
            if (missing.empty()) {
                WriteParity(reason, 0, {});   // nothing planned: an older run's file must not stand in
                SetStatus("upToDate", reason, registered.spells.size());
                return;
            }

            json rules;
            std::vector<std::string> problems;
            std::vector<Adapters::File> files(1);
            if (!ReadJsonFile(RulesPath() / RULES_FILE, rules) || !Adapters::ParseFile(rules, "spellresearch.json", files[0], problems)) {
                logger::warn("SpellResearch: {} not readable - nothing registered", RULES_FILE);
                WriteParity(reason, missing.size(), {});
                return;
            }
            for (const auto& problem : problems) logger::warn("SpellResearch: {}", problem);

            // The SR file alone: formListsNone reads the marks of this one plan.
            // Only the missing spells: a line's pick is per spell, so the rest
            // would change nothing and cost their JSON.
            LoadOrderSource loadOrder(missing);
            std::unordered_map<std::string, RE::BGSListForm*> lists;
            const auto listOf = [&](const std::string& id) {
                const auto found = lists.find(id);
                if (found != lists.end()) return found->second;
                return lists.emplace(id, ResolveList(id)).first->second;
            };
            Adapters::PlanSource source;
            source.forEachCatalogSpell = [&](const auto& visit) { loadOrder.ForEachCatalogSpell(visit); };
            source.usersOf = [&](const std::string& key) { return loadOrder.UsersOf(key); };
            source.formListDefined = [&](const std::string& id) { return listOf(id) != nullptr; };
            source.inFormList = [&](const std::string& id, const std::string& spellId) {
                auto* list = listOf(id);
                auto* spell = RE::TESForm::LookupByID(SpellScanner::ResolvePersistentFormId(spellId));
                return list && spell && list->HasForm(spell);
            };
            const Adapters::Plan plan = Adapters::BuildPlan(files, source);
            steps.Mark("plan");
            const SR::Registrations registrations = SR::Build(g_lists, plan, missing, [&](const std::string& id) {
                const auto tome = tomes.find(SpellScanner::ResolvePersistentFormId(id));
                return tome == tomes.end() ? std::string() : SpellScanner::GetPersistentFormId(tome->second->GetFormID());
            });
            WriteParity(reason, missing.size(), registrations);

            json report;
            report["reason"] = std::string(reason);
            report["problems"] = problems;
            report["alreadyRegistered"] = registered.spells.size();
            report["noSchoolOrTier"] = registrations.noSchoolOrTier;
            report["leftOut"] = { { "voiceSlot", skipped.voiceSlot }, { "gate", skipped.gateOut },
                                  { "duplicate", skipped.duplicate }, { "tomeUnreachable", skipped.unreachable },
                                  { "player", skipped.playerLeftOut } };
            const Totals totals = Register(registrations, report);
            g_lastSeen = CountRegistered();
            steps.Mark("register");

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            report["registered"] = totals.spells;
            report["entries"] = totals.entries;
            report["tomes"] = totals.tomes;
            report["failed"] = totals.failed;
            report["elapsedMs"] = elapsed;
            report["lines"] = Adapters::PlanReport(files, plan);
            WriteReport(report);

            logger::info("SpellResearch: {} - {} spells registered ({} list entries, {} tomes), {} already known to SR, "
                         "{} would not take, {} tome tiers out of step, in {} ms",
                reason, totals.spells, totals.entries, totals.tomes, registered.spells.size(), totals.failed,
                totals.tomeTiersSkipped, elapsed);
            logger::info("SpellResearch: {} - stages (ms) - {}", reason, steps.Text());

            SetStatus(totals.spells > 0 ? "added" : "upToDate", reason, registered.spells.size() + totals.spells, totals.spells);
            // SR's own import quests say what they brought in ("Importing 46 Forms from
            // Dawnguard"); without this the player never saw that these spells came in too
            if (totals.spells > 0) {
                HudNotify::Show("hud.spellResearchAdded", "Spell Research: {{count}} spells added by Heart of Magic",
                    { { "count", std::to_string(totals.spells) } });
            }
        }

        void SafeReconcile(std::string_view reason)
        {
            try {
                Reconcile(reason);
            } catch (const std::exception& e) {
                logger::error("SpellResearch: {} failed - {}", reason, e.what());
            }
        }

        // A possible end of an Import, checked on the game thread. Safe to repeat: Reconcile does nothing
        // while the tier lists hold what the last run left (g_lastSeen); the same pairs make no new generation.
        void QueueImportCheck(const char* reason)
        {
            // The tome pairs first: an Import that ends with the same entry count still re-paired
            AddTaskToGameThread("SpellResearchImport", [reason] {
                SpellTomes::Rebuild(SpellTomes::kReasonImport);
                SafeReconcile(reason);
            });
        }

        // SR shows "All Spells Imported" in a message box when an Import ends.
        // Any message box opening or closing is checked; the tier lists' size
        // tells. On opening: the Import's globals are set before SR asks for the
        // box, so the spells are in (and announced) while the player reads it.
        class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static MenuSink* GetSingleton()
            {
                static MenuSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (event && event->menuName == RE::MessageBoxMenu::MENU_NAME) QueueImportCheck("import");
                return RE::BSEventNotifyControl::kContinue;
            }

            MenuSink(const MenuSink&) = delete;
            MenuSink& operator=(const MenuSink&) = delete;

        private:
            MenuSink() = default;
        };

        // The same check without a message box: SR's Papyrus sends IMPORT_FINISHED_EVENT once its globals
        // are set. Every mod's events come through here (Papyrus thread); others return at the name, read
        // case-blind as Papyrus pools its strings.
        class ModEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
        {
        public:
            static ModEventSink* GetSingleton()
            {
                static ModEventSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
                RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
            {
                if (event && AsciiText::EqualsIgnoreCase(event->eventName.c_str(), IMPORT_FINISHED_EVENT)) {
                    QueueImportCheck("import event");
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            ModEventSink(const ModEventSink&) = delete;
            ModEventSink& operator=(const ModEventSink&) = delete;

        private:
            ModEventSink() = default;
        };
    }

    void Install()
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || !PluginLoaded(dataHandler, SR_PLUGIN)) return;

        json rules;
        std::vector<std::string> problems;
        if (!ReadJsonFile(RulesPath() / RULES_FILE, rules) || !SR::ParseLists(rules, g_lists, problems)) {
            for (const auto& problem : problems) logger::warn("SpellResearch: {}", problem);
            logger::warn("SpellResearch: {} is loaded but {} is missing or incomplete - no registrations", SR_PLUGIN, RULES_FILE);
            return;
        }
        if (!ResolveList(g_lists.allSpells[0]) || !SpellScanner::ResolvePersistentFormId(g_lists.isSetup)) {
            logger::warn("SpellResearch: {} does not have the lists {} names - another version? No registrations",
                SR_PLUGIN, RULES_FILE);
            return;
        }

        RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        if (auto* modEvents = SKSE::GetModCallbackEventSource()) {
            modEvents->AddEventSink(ModEventSink::GetSingleton());
        } else {
            logger::warn("SpellResearch: no SKSE mod event source - '{}' is not heard, only SR's message box",
                IMPORT_FINISHED_EVENT);
        }
        g_installed = true;
        logger::info("SpellResearch: {} found - catalog spells are registered when its Import finishes (message box "
                     "or mod event '{}') and at save load", SR_PLUGIN, IMPORT_FINISHED_EVENT);
    }

    void OnGameLoaded()
    {
        ++g_gameSession;
        g_waitingForPass = false;
        g_lastSeen = 0;
        g_failed.clear();
        SafeReconcile("load");
    }

    void OnNewGame()
    {
        ++g_gameSession;
        g_waitingForPass = false;
        g_lastSeen = 0;
        g_failed.clear();
        std::lock_guard lock(g_statusLock);
        g_status = json();
    }

    void OnCatalogChanged()
    {
        g_lastSeen = 0;
        g_failed.clear();
        SafeReconcile("scan");
    }
}

namespace Librarian::SpellResearchPatch
{
    std::string StatusJson()
    {
        if (!g_installed) return {};
        std::lock_guard lock(g_statusLock);
        return g_status.is_null() ? std::string() : JsonText::Dump(g_status);
    }
}

namespace Librarian::SpellResearchPatch
{
    TomeListState ReadTomeLists(TomeLists& out)
    {
        if (!g_installed) return TomeListState::kNotInstalled;
        if (GlobalValue(g_lists.isSetup) != 1.0f || GlobalValue(g_lists.importing) != 0.0f) return TomeListState::kNotReady;
        for (std::size_t t = 0; t < out.size(); ++t) out[t] = { ResolveList(g_lists.tomes[t]), ResolveList(g_lists.tomeSpells[t]) };
        return TomeListState::kReady;
    }
}
