#include "Common.h"
#include "SpellTomes.h"
#include "ThreadUtils.h"
#include "tomereach/TomeReach.h"
#include "tomereach/TomeReachConfig.h"
#include "tomereach/TomeReachPass.h"
#include "tomereach/TomeReachSources.h"
#include "ScanRecordRules.h"
#include "SpellScanner.h"
#include "environment/EnvironmentSelfTest.h"

#include <algorithm>
#include <format>
#include <string>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace TomeReach
{
    namespace
    {
        // What the worker needs, taken on the game thread: no RE object crosses over
        struct Job
        {
            std::uint64_t generation = 0;   // SpellTomes' pairs the tomes come from
            std::vector<PluginFile> loadOrder;
            NameTable names;
            std::unordered_set<Key> tomes;
            std::unordered_map<RE::FormID, std::vector<Key>> tomesOf;  // spell -> its tomes
            std::unordered_map<Key, Key> tomeSpells;                     // tome -> its spell's key (the model)
            std::unordered_map<RE::FormID, Key> spellKeys;               // spell -> its key
            bool logQuestOnly = false;   // the dev self-test is on (selftest.json): the quest-only list is logged
            std::unordered_map<RE::FormID, std::string> spellLabels;   // spell -> "'name' formId persistentId" (only then)
        };

        struct Result
        {
            std::mutex lock;
            std::condition_variable changed;
            bool started = false;
            std::uint64_t generation = 0;   // the pass whose result counts
            std::unordered_set<RE::FormID> keyedBooks;     // tome books handed to that pass
            std::unordered_set<RE::FormID> skippedBooks;   // ... left out: plugin not in the loaded set
            bool done = false;    // finished, well or not
            bool ready = false;   // finished well: `unreachable` and `questOnly` hold the answer
            std::string failure;  // finished without an answer: what failed, for OutcomeKey
            std::unordered_set<RE::FormID> unreachable;
            std::unordered_set<RE::FormID> questOnly;
            bool model = false;   // the distribution model answered: `sources` holds the segments
            std::unordered_map<RE::FormID, SpellSourceInfo> sources;
        };

        // A pass's answer: the spells nothing hands a tome of, the reach pass's quest-only ones,
        // and the model's sources (when it ran)
        struct Answer
        {
            std::unordered_set<RE::FormID> unreachable;
            std::unordered_set<RE::FormID> questOnly;
            bool model = false;
            std::unordered_map<RE::FormID, SpellSourceInfo> sources;
        };

        Result& Shared()
        {
            static Result result;
            return result;
        }

        // The pass is over, with an answer or without one (then `failure` says what failed). A pass
        // of an older generation is dropped: a newer one runs (or ran) with other tomes.
        void Finish(std::uint64_t generation, Answer* answer, std::string failure = {})
        {
            auto& shared = Shared();
            {
                std::lock_guard guard(shared.lock);
                if (generation != shared.generation) {
                    logger::info("TomeReach: the pass of generation {} is dropped - generation {} counts",
                        generation, shared.generation);
                    return;
                }
                if (answer) {
                    shared.unreachable = std::move(answer->unreachable);
                    shared.questOnly = std::move(answer->questOnly);
                    shared.model = answer->model;
                    shared.sources = std::move(answer->sources);
                    shared.ready = true;
                } else {
                    shared.failure = std::move(failure);
                }
                shared.done = true;
            }
            shared.changed.notify_all();
        }

        // A form's key: its plugin's name and its id inside that plugin
        bool KeyOf(RE::TESDataHandler* dh, RE::FormID formId, NameTable& names, Key& out)
        {
            const RE::TESFile* file = nullptr;
            std::uint32_t local = 0;
            if ((formId >> kFileIndexShift) == kLightIndex) {
                file = dh->LookupLoadedLightModByIndex(
                    static_cast<std::uint16_t>((formId >> kLightFileShift) & kLightFileMask));
                local = formId & kLightLocalMask;
            } else {
                file = dh->LookupLoadedModByIndex(static_cast<std::uint8_t>(formId >> kFileIndexShift));
                local = formId & kLocalMask;
            }
            if (!file) return false;
            out = MakeKey(names.Id(file->GetFilename()), local);
            return true;
        }

        // Every quest-only spell of the load order, one line each, and their count: the list the
        // author looks over before a release (the tree graft design, decision 3; Ruling 20: only
        // with the dev self-test on, each pass that answers). Sorted by formId.
        void LogQuestOnly(const Job& job, const PassResult& pass, const std::unordered_set<RE::FormID>& questOnly)
        {
            std::vector<RE::FormID> spells(questOnly.begin(), questOnly.end());
            std::ranges::sort(spells);
            for (const RE::FormID spell : spells) {
                std::vector<std::string> plugins;   // whose quest records hand out its reached tomes
                if (const auto tomes = job.tomesOf.find(spell); tomes != job.tomesOf.end()) {
                    for (const Key tome : tomes->second) {
                        const auto from = pass.questFrom.find(tome);
                        if (from == pass.questFrom.end()) continue;
                        for (const std::uint32_t plugin : from->second) {
                            const auto& name = job.names.Name(plugin);
                            if (std::ranges::find(plugins, name) == plugins.end()) plugins.push_back(name);
                        }
                    }
                }
                std::string joined;
                for (const auto& name : plugins) joined += (joined.empty() ? "" : ", ") + name;
                const auto label = job.spellLabels.find(spell);
                logger::info("TomeReach quest-only: {} - quest records in {}",
                    label != job.spellLabels.end() ? label->second : std::format("{:08X}", spell),
                    joined.empty() ? std::string("(unknown)") : joined);
            }
            logger::info("TomeReach quest-only: {} spells (tome pairs generation {})", spells.size(), job.generation);
        }

        // Every spell off the tree by its source with the switches now, one line each (the
        // dev self-test's counterpart of LogQuestOnly for the model). Sorted by formId.
        void LogOffTree(const Job& job, const Answer& answer)
        {
            const SegmentSwitches switches = ReadSegmentSwitches();
            std::vector<RE::FormID> spells;
            for (const auto& [spell, s] : answer.sources) {
                if (s.segment != Segment::kUnreachable && !switches.OnTree(s.segment)) spells.push_back(spell);
            }
            std::ranges::sort(spells);
            for (const RE::FormID spell : spells) {
                const auto& s = answer.sources.at(spell);
                const auto label = job.spellLabels.find(spell);
                logger::info("TomeReach off the tree: {} - {} ({} {}): {}",
                    label != job.spellLabels.end() ? label->second : std::format("{:08X}", spell), SegmentName(s.segment),
                    ClassName(s.cls), s.subtype, s.detail);
            }
            logger::info("TomeReach off the tree: {} spells (tome pairs generation {})", spells.size(), job.generation);
        }

        void Run(std::shared_ptr<Job> job)
        {
            const auto started = std::chrono::steady_clock::now();
            try {
                const auto configs = FindDistributionConfigs(std::filesystem::path(kDataDir));
                const auto pass = FindReachedTomes(job->loadOrder, job->names, job->tomes, configs);
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                if (!pass.unreadable.empty()) {
                    logger::warn("TomeReach: {} plugins could not be read (first: {}) - no spell is marked, "
                                 "every tome counts", pass.unreadable.size(), pass.unreadable.front());
                    // The same plugins fail the same way next start: their names make the outcome
                    auto names = pass.unreadable;
                    std::ranges::sort(names);
                    std::string joined;
                    for (const auto& name : names) joined += name + "\n";
                    Finish(job->generation, nullptr, "unreadable:" + ScanRecordRules::HashText(joined));
                    return;
                }
                Answer answer;
                for (const auto& [spell, tomes] : job->tomesOf) {
                    if (NoTomeReached(tomes, pass)) answer.unreachable.insert(spell);
                    if (OnlyQuestsReach(tomes, pass)) answer.questOnly.insert(spell);
                }
                for (const auto& d : pass.damaged) {
                    logger::warn("TomeReach: '{}' is damaged (read in part) - the tomes of it and its masters count "
                                 "as handed out", d);
                }
                logger::info("TomeReach: {} plugins, {} tomes, {} handed out by a record, {} only by a script "
                             "property or alias, {} only by a distribution config ({} configs), {} kept for a damaged "
                             "plugin, {} debug tomes left out; {} spells have no tome anything hands out, {} only "
                             "tomes a quest hands out ({} such tomes) ({} ms, tome pairs generation {})",
                    job->loadOrder.size(), job->tomes.size(), pass.byRecord, pass.byLooseOnly, pass.byConfig,
                    pass.configFiles, pass.byDamaged, pass.byDebug, answer.unreachable.size(), answer.questOnly.size(),
                    pass.questOnly.size(), ms, job->generation);
                // The distribution model: its unreachable segment replaces the pass's, and its
                // segments decide questOnly (QuestOnlySpells); the pass answers when it cannot run
                if (ClassifyForGame(job->loadOrder, job->names, job->tomeSpells, job->spellKeys, answer.sources)) {
                    const std::size_t passUnreachable = answer.unreachable.size();
                    answer.unreachable.clear();
                    std::size_t unclassified = 0;
                    for (const auto& [spell, tomes] : job->tomesOf) {
                        const auto source = answer.sources.find(spell);
                        if (source == answer.sources.end()) {
                            // No tome of it in the model's store: the pass's mark stands
                            ++unclassified;
                            if (NoTomeReached(tomes, pass)) answer.unreachable.insert(spell);
                        } else if (source->second.segment == Segment::kUnreachable) {
                            answer.unreachable.insert(spell);
                        }
                    }
                    answer.model = true;
                    logger::info("TomeReach: the model marks {} spells unreachable (the reach pass {}); {} spells "
                                 "the model has no tome of keep the pass's mark ({} ms in all)", answer.unreachable.size(),
                        passUnreachable, unclassified, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                            std::chrono::steady_clock::now() - started).count());
                    if (job->logQuestOnly) LogOffTree(*job, answer);
                } else if (job->logQuestOnly) {
                    LogQuestOnly(*job, pass, answer.questOnly);
                }
                Finish(job->generation, &answer);
            } catch (const std::exception& e) {
                logger::warn("TomeReach: the pass failed ({}) - no spell is marked", e.what());
                Finish(job->generation, nullptr, "error");
            } catch (...) {
                // Whatever went wrong, a scan waiting for the pass must not wait on
                logger::warn("TomeReach: the pass failed - no spell is marked");
                Finish(job->generation, nullptr, "error");
            }
        }
    }

    void StartAfterDataLoaded()
    {
        Restart(SpellTomes::Generation());
    }

    void Restart(std::uint64_t generation)
    {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return;

        auto job = std::make_shared<Job>();
        job->generation = generation;
        job->logQuestOnly = Environment::SelfTest::IsOn();
        std::unordered_set<RE::FormID> keyedBooks, skippedBooks;
        std::unordered_set<const RE::TESFile*> loaded;
        for (std::uint32_t i = 0; i < dh->GetLoadedModCount(); ++i) loaded.insert(dh->GetLoadedMods()[i]);
        for (std::uint32_t i = 0; i < dh->GetLoadedLightModCount(); ++i) loaded.insert(dh->GetLoadedLightMods()[i]);
        for (const RE::TESFile* file : dh->files) {
            if (!file || !loaded.contains(file)) continue;
            // The engine's own folder for the file, and Data/ when that one cannot be opened
            const std::string name(file->GetFilename());
            const std::string_view dir(file->path);
            const std::filesystem::path data(kDataDir);
            const std::filesystem::path path = (dir.empty() ? data : std::filesystem::path(dir)) / name;
            job->loadOrder.push_back({ name, path, path == data / name ? std::filesystem::path() : data / name });
            job->names.SetLight(job->names.Id(file->GetFilename()), file->IsLight());
        }

        // Every tome, a book that teaches nothing but has a Spell Research pair too
        SpellTomes::ForEachTome([&](RE::TESObjectBOOK* book, RE::SpellItem* spell) {
            Key key = 0;
            if (!KeyOf(dh, book->GetFormID(), job->names, key)) {
                skippedBooks.insert(book->GetFormID());
                return;
            }
            keyedBooks.insert(book->GetFormID());
            job->tomes.insert(key);
            const RE::FormID spellId = spell->GetFormID();
            job->tomesOf[spellId].push_back(key);
            if (Key spellKey = 0; KeyOf(dh, spellId, job->names, spellKey)) {
                job->tomeSpells[key] = spellKey;
                job->spellKeys[spellId] = spellKey;
            }
            if (job->logQuestOnly && !job->spellLabels.contains(spellId)) {
                const char* name = spell->GetName();
                job->spellLabels.emplace(spellId, std::format("'{}' {:08X} {}", name ? name : "", spellId,
                    SpellScanner::GetPersistentFormId(spellId)));
            }
        });
        logger::info("TomeReach: reading {} plugins for {} tomes on a worker thread (first: '{}', tome pairs generation {})",
            job->loadOrder.size(), job->tomes.size(),
            job->loadOrder.empty() ? std::string() : job->loadOrder.front().path.string(), generation);

        auto& shared = Shared();
        {
            // This generation's answer from now on: a pass still running for an older one is dropped
            std::lock_guard guard(shared.lock);
            shared.generation = generation;
            shared.keyedBooks = std::move(keyedBooks);
            shared.skippedBooks = std::move(skippedBooks);
            shared.done = false;
            shared.ready = false;
            shared.failure.clear();
            shared.unreachable.clear();
            shared.questOnly.clear();
            shared.model = false;
            shared.sources.clear();
        }
        try {
            std::thread(Run, std::move(job)).detach();
        } catch (const std::system_error& e) {
            logger::warn("TomeReach: no worker thread ({}) - no spell is marked", e.what());
            {
                // A scan already waiting on an earlier pass must not wait on
                std::lock_guard guard(shared.lock);
                shared.failure = "no thread";
                shared.done = true;
            }
            shared.changed.notify_all();
            return;
        }
        // Marked after the thread exists: a scan only waits for a pass that runs
        // (one already done lets the wait return at once)
        std::lock_guard guard(shared.lock);
        shared.started = true;
    }

    bool WaitUntilDone(std::chrono::milliseconds limit, std::uint64_t* generation)
    {
        auto& shared = Shared();
        std::unique_lock guard(shared.lock);
        if (generation) *generation = shared.generation;
        if (!shared.started) return false;
        const bool done = shared.changed.wait_for(guard, limit, [&] { return shared.done; });
        if (generation) *generation = shared.generation;
        return done;
    }

    bool IsRunning()
    {
        auto& shared = Shared();
        std::lock_guard guard(shared.lock);
        return shared.started && !shared.done;
    }

    bool TomeBooks(std::uint64_t generation, std::unordered_set<RE::FormID>& keyed, std::unordered_set<RE::FormID>& skipped)
    {
        auto& shared = Shared();
        std::lock_guard guard(shared.lock);
        if (shared.generation != generation) return false;
        keyed = shared.keyedBooks;
        skipped = shared.skippedBooks;
        return true;
    }

    bool RunAfterPass(std::string name, std::function<void(PassOutcome outcome)> task)
    {
        try {
            std::thread([name, task = std::move(task)]() mutable {
                // Nothing may leave a thread function: it would end the game
                try {
                    constexpr auto kTomeReachWaitLimit = std::chrono::minutes(10);
                    // The generation read under the wait's own lock: a Restart right after it
                    // then shows as a newer generation (kSuperseded), not as this one failing
                    std::uint64_t waited = 0;
                    const bool done = WaitUntilDone(kTomeReachWaitLimit, &waited);
                    AddTaskToGameThread(name, [done, waited, name, task = std::move(task)] {
                        // Read together: a Restart between the wait and here starts a newer pass
                        auto& shared = Shared();
                        std::uint64_t current = 0;
                        bool ready = false;
                        bool running = false;
                        {
                            std::lock_guard guard(shared.lock);
                            current = shared.generation;
                            ready = shared.ready;
                            running = shared.started && !shared.done;
                        }
                        const PassOutcome outcome = OutcomeOf(done, waited, current, ready, running);
                        if (outcome == PassOutcome::kSuperseded) {
                            logger::info("TomeReach: '{}' waited for generation {}, generation {} runs now", name,
                                waited, current);
                        }
                        task(outcome);
                    });
                } catch (...) {
                    logger::error("TomeReach: '{}' could not be handed to the game thread after the pass", name);
                }
            }).detach();
            return true;
        } catch (const std::system_error& e) {
            logger::warn("TomeReach: no thread to wait for the pass ({}) - '{}' does not run after it", e.what(), name);
            return false;
        }
    }

    std::string OutcomeKey()
    {
        auto& shared = Shared();
        std::lock_guard guard(shared.lock);
        if (!shared.started) return "not started";
        if (!shared.done) return {};
        return shared.ready ? std::string("ready") : "failed:" + shared.failure;
    }

    bool UnreachableSpells(std::unordered_set<RE::FormID>& out)
    {
        auto& shared = Shared();
        std::lock_guard guard(shared.lock);
        if (!shared.ready) return false;
        out = shared.unreachable;
        return true;
    }

    bool QuestOnlySpells(std::unordered_set<RE::FormID>& out)
    {
        const SegmentSwitches switches = ReadSegmentSwitches();   // config.json, before the lock
        auto& shared = Shared();
        std::lock_guard guard(shared.lock);
        if (!shared.ready) return false;
        if (!shared.model) {
            out = shared.questOnly;
            return true;
        }
        out.clear();
        for (const auto& [spell, s] : shared.sources) {
            if (s.segment != Segment::kUnreachable && !switches.OnTree(s.segment)) out.insert(spell);
        }
        return true;
    }

    bool SourcesOfSpells(std::unordered_map<RE::FormID, SpellSourceInfo>& out)
    {
        auto& shared = Shared();
        std::lock_guard guard(shared.lock);
        if (!shared.ready || !shared.model) return false;
        out = shared.sources;
        return true;
    }
}
