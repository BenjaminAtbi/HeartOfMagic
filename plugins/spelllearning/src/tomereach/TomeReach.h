#pragma once

// =============================================================================
// TOME REACH - IN GAME
// =============================================================================
//
// After the data loads, a worker thread reads the load order's plugin files and
// the Data folder's configs and scripts, and puts every spell with a tome in a
// distribution segment (the distribution model, TomeReachModel.h): no route
// (tomeUnreachable), general sale, wide drops, uncommon vendor, specialty mod
// vendor, specific loot, quest reward, crafted only. The settings switch each
// segment on or off the tree (config.json tomeSources); a spell of a segment that
// is off is questOnly. The older reach pass (TomeReachPass: any record or config
// that names a tome) runs only when the model cannot (a plugin it could not read,
// or it threw), and answers then.
// See docs/ARCHITECTURE.md, "Tomes nothing hands out".

#include "tomereach/TomeReachOutcome.h"
#include "tomereach/TomeReachSegments.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace TomeReach
{
    // Game thread, at kDataLoaded: takes the load order and the tomes, starts the pass
    void StartAfterDataLoaded();

    // Game thread: the tome pairs changed (SpellTomes.h, generation `generation`):
    // the pass runs again with this generation's tomes. Until it is done no spell
    // is marked; a pass of an older generation that finishes later is dropped.
    void Restart(std::uint64_t generation);

    // The spells whose every tome nothing hands out (the model's unreachable segment). False
    // while the pass runs, and when it failed (a plugin it could not read): then no spell is marked.
    bool UnreachableSpells(std::unordered_set<RE::FormID>& out);

    // The spells off the tree by their source: a segment whose switch is off
    // (ReadSegmentSwitches, read on each call). When the model could not run, the reach
    // pass's quest-only spells: at least one reached tome, every reached one only a quest
    // hands out (PassResult::questOnly, OnlyQuestsReach). False when UnreachableSpells is.
    // With the dev self-test on (selftest.json) the pass logs each one when it finishes.
    bool QuestOnlySpells(std::unordered_set<RE::FormID>& out);

    // A spell's source as the model found it: its segment and most generic route
    struct SpellSourceInfo
    {
        Segment segment = Segment::kUnreachable;
        SourceClass cls = SourceClass::kNone;
        std::string subtype;
        std::string detail;
        std::string shop;   // specialty mod vendor: the plugin whose tomes the chest sells
    };

    // Every spell with a tome and its source. False while the pass runs, when it failed,
    // and when the model could not run (the reach pass answered). Any thread.
    bool SourcesOfSpells(std::unordered_map<RE::FormID, SpellSourceInfo>& out);

    // The scan's source object of a spell: { segment (SegmentKey), class, subtype, detail, shop? }
    nlohmann::json SourceJson(const SpellSourceInfo& info);

    // The tree switches of the segments: config.json "tomeSources" ({ "<SegmentKey>": bool }),
    // the defaults (SegmentSwitches) for what is missing. Any thread.
    SegmentSwitches ReadSegmentSwitches();

    // Any thread but the game thread: waits until the pass is done (or failed),
    // at most `limit`. Returns at once when no pass was started. True when done.
    // `generation`, when given: the generation as the wait ended, read under the same
    // lock (RunAfterPass tells a newer pass from a timeout by it).
    bool WaitUntilDone(std::chrono::milliseconds limit, std::uint64_t* generation = nullptr);

    // A pass was started and is not done yet (the answer is coming). Any thread.
    bool IsRunning();

    // The tome books handed to the pass of `generation`, and the ones left out because
    // their plugin is not in the loaded set (KeyOf). False when the last pass is not that
    // generation's. Any thread (the dev self-test).
    bool TomeBooks(std::uint64_t generation, std::unordered_set<RE::FormID>& keyed, std::unordered_set<RE::FormID>& skipped);

    // `task(outcome)` on the game thread once the pass running now is done (or at
    // once when none runs). kAnswered: it finished with an answer (UnreachableSpells
    // gives one); kFailed: it failed or the wait ran out; kSuperseded: the tome pairs
    // changed before the task ran and a newer pass runs - wait for that one
    // (TomeReachOutcome.h). The wait is on a thread of its own. False (logged) when no
    // thread could be made: `task` does not run.
    bool RunAfterPass(std::string name, std::function<void(PassOutcome outcome)> task);

    // How the pass ended, for the scan record (ScanRecordRules::WithTomeReach):
    // "ready", "failed:unreadable:<FNV of the sorted unreadable plugin names>",
    // "failed:error" (it threw), "not started"; empty while it runs. A pass that
    // fails the same way again gives the same key, so its scan stays recorded.
    // Any thread.
    std::string OutcomeKey();
}
