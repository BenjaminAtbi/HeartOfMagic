#pragma once

// =============================================================================
// TOME REACH - CONTAINER DISTRIBUTION FRAMEWORK (distribution model)
// =============================================================================
//
// CDF rules (SKSE/Plugins/ContainerDistributionFramework/*.json) as the plugin
// applies them (SeaSparrow's DynamicContainerInventoryFramework, JSONSettings.cpp):
// - files directly in the folder, in path order;
// - a syntax error keeps the rules read before it (jsoncpp's lenient reader; the rule
//   the error cuts is left out);
// - a rule without friendlyName or a changes array ends its file, and so does a
//   `plugins` condition naming an inactive plugin;
// - each change object is one change: add [forms], remove "form" (one string: an array
//   drops the change), add + remove = replace, removeByKeywords [keywords]; an add entry
//   that does not resolve drops the change;
// - containers: the `containers` base forms; vendor chests only with allowVendors or
//   onlyVendors. Other conditions (locations, globals, ...) only narrow where or when.
// The reach graph turns an add into a container parent, and a replace or an uncounted
// remove takes the removed form out of the container. Port of the author's Python
// (scripts/spellmap/core.py cdf_parse, cdf_changes; reach.py Graph._cdf). Pure C++,
// shared by the plugin and tools/tome-reach-test.

#include "tomereach/TomeReachToken.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace TomeReach
{
    // One change as written, forms not yet resolved
    struct RawCdfChange
    {
        std::string rule;                       // friendlyName
        std::string source;                     // the file's data path
        std::vector<FormToken> containers;      // the `containers` condition's string entries
        bool vendors = false;                   // allowVendors or onlyVendors
        bool hasAdd = false;                    // an `add` array was given
        std::size_t addEntries = 0;             // its entries, strings or not (each must resolve)
        std::vector<FormToken> add;
        bool hasRemove = false;                 // a non-empty `remove` string
        FormToken remove;
        bool removeKeywords = false;            // a non-empty removeByKeywords
        bool countGiven = false;                // `count` given: a remove takes that many, not all
    };

    // One file's rules -> its changes; what CDF rejects or never reads goes to `warnings`.
    // `activePlugins`: the load order's plugin names, lower case (a `plugins` condition).
    void ReadCdf(std::string_view text, const std::string& source, const std::unordered_set<std::string>& activePlugins,
        NameTable& names, std::vector<RawCdfChange>& out, std::vector<std::string>& warnings);

    enum class CdfOp : std::uint8_t { kAdd, kRemove, kReplace, kRemoveKeywords, kReplaceKeywords };

    struct CdfChange
    {
        std::string rule;
        std::vector<Key> containers;            // empty: any container the other conditions allow (not traced)
        bool vendors = false;
        CdfOp op = CdfOp::kAdd;
        std::vector<Key> add;
        std::vector<Key> remove;                // kRemove / kReplace: the one removed form
        bool countGiven = false;
    };

    // Resolves the changes' forms; a change whose add entries or removed form do not all
    // resolve is dropped (as CDF drops it), and so is one that changes nothing
    std::vector<CdfChange> ResolveCdf(const std::vector<RawCdfChange>& raw, const SpellStore& store,
        std::vector<std::string>& warnings);
}
