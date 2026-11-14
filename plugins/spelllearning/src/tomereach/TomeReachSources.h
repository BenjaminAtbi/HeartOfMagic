#pragma once

// =============================================================================
// TOME REACH - THE DISTRIBUTION MODEL IN GAME
// =============================================================================
//
// The worker thread's second step (TomeReach.cpp): the distribution model
// (TomeReachModel.h) over the loaded plugins and the Data folder, its answer per
// spell form id. And the tree switches of the segments, read from config.json.

#include "tomereach/TomeReach.h"
#include "tomereach/TomeReachRecords.h"

#include <unordered_map>
#include <vector>

namespace TomeReach
{
    // `tomeSpells`: tome key -> its spell's key (SpellTomes' pairs); `spellKeys`: spell form id -> its key.
    // False (logged) when the model could not run: a plugin it could not read, or it threw.
    bool ClassifyForGame(const std::vector<PluginFile>& loadOrder, NameTable& names,
        const std::unordered_map<Key, Key>& tomeSpells, const std::unordered_map<RE::FormID, Key>& spellKeys,
        std::unordered_map<RE::FormID, SpellSourceInfo>& out);
}
