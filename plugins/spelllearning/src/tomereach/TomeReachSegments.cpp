#include "tomereach/TomeReachSegments.h"
#include "AsciiText.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace TomeReach
{
    namespace
    {
        constexpr std::size_t kMaxShops = 3;         // chests of a spellpack vendor
        constexpr std::size_t kCoverNumerator = 3;   // ... that sell 3/4 of the set
        constexpr std::size_t kCoverDenominator = 4;
        constexpr std::size_t kMinSet = 3;           // vendor-sold tome spells a plugin needs to have a shop
        constexpr std::array<std::string_view, 5> kBasePlugins{ "skyrim.esm", "update.esm", "dawnguard.esm",
            "dragonborn.esm", "hearthfires.esm" };
        constexpr std::size_t kShopSale = 3;         // open vendors a spellpack vendor's spell may have, at most
        constexpr std::size_t kStandardVendors = 3;  // open vendors that make standard sale
        constexpr std::uint8_t kAlwaysNone = 100;    // a list's chance of nothing that closes it until a global opens it
        constexpr std::string_view kRitualGlobal = "mgritual";   // the College's master ritual globals
        constexpr std::string_view kSkillGlobal = "pc";          // vanilla's skill thresholds: ordinary stock
        const std::string kNoName;

        // A vendor chest's stock of a spell: open, unlocked by a master ritual quest, or by another quest
        enum class Gate : std::uint8_t { kOpen, kRitual, kQuest };

        // The gate of one entry a vendor chest holds: a list a global holds at 100% chance of nothing
        Gate GateOf(const SpellStore& store, Key entry)
        {
            const auto list = store.listGates.find(entry);
            if (list == store.listGates.end() || list->second.chanceNone < kAlwaysNone) return Gate::kOpen;
            const auto global = store.globalIds.find(list->second.global);
            const std::string id = AsciiText::ToLower(global != store.globalIds.end() ? global->second : kNoName);
            if (id.starts_with(kSkillGlobal)) return Gate::kOpen;
            return id.starts_with(kRitualGlobal) ? Gate::kRitual : Gate::kQuest;
        }

        using Chests = std::set<Key>;

        // The group of up to kMaxShops chests that sells the most spells of `set` (the first of equal ones, chests
        // in editor id order)
        std::pair<std::size_t, std::vector<Key>> BestCover(const std::vector<Key>& set,
            const std::unordered_map<Key, Chests>& sells, const std::vector<Key>& order)
        {
            std::set<Key> all;
            for (const Key s : set) {
                if (const auto it = sells.find(s); it != sells.end()) all.insert(it->second.begin(), it->second.end());
            }
            std::vector<Key> chests;
            for (const Key c : order) {
                if (all.contains(c)) chests.push_back(c);
            }
            const auto covered = [&](const std::vector<Key>& group) {
                std::size_t n = 0;
                for (const Key s : set) {
                    const auto it = sells.find(s);
                    if (it == sells.end()) continue;
                    if (std::any_of(group.begin(), group.end(), [&](Key c) { return it->second.contains(c); })) ++n;
                }
                return n;
            };
            std::pair<std::size_t, std::vector<Key>> best{ 0, {} };
            const auto consider = [&](std::vector<Key> group) {
                if (const std::size_t n = covered(group); n > best.first) best = { n, std::move(group) };
            };
            // Groups by size, each size in combination order (Python's itertools.combinations)
            for (std::size_t i = 0; i < chests.size(); ++i) consider({ chests[i] });
            for (std::size_t i = 0; i < chests.size(); ++i) {
                for (std::size_t j = i + 1; j < chests.size(); ++j) consider({ chests[i], chests[j] });
            }
            for (std::size_t i = 0; i < chests.size(); ++i) {
                for (std::size_t j = i + 1; j < chests.size(); ++j) {
                    for (std::size_t k = j + 1; k < chests.size(); ++k) consider({ chests[i], chests[j], chests[k] });
                }
            }
            return best;
        }
    }

    SpellSources ClassifySpells(const SpellStore& store, const NameTable& names, Classifier& classifier)
    {
        SpellSources out;
        for (const auto& [tome, spell] : store.bookSpells) out.spells[spell].tomes.push_back(tome);
        const auto editorId = [&](Key k) -> const std::string& {
            const auto it = store.editorIds.find(k);
            return it != store.editorIds.end() ? it->second : kNoName;
        };
        std::unordered_map<Key, std::string> plugins;   // spell -> its plugin, lower case
        for (const auto& [spell, _s] : out.spells) plugins.emplace(spell, AsciiText::ToLower(names.Name(PluginOf(spell))));
        const auto pluginOf = [&](Key spell) -> const std::string& { return plugins.at(spell); };

        // Tome routes, and per spell the vendor chests above its tomes with the entries they hold
        std::unordered_map<Key, std::vector<VendorEntries>> sold;
        std::unordered_map<Key, std::set<Key>> reach;   // entry -> the spells sold through it
        for (auto& [spell, s] : out.spells) {
            std::sort(s.tomes.begin(), s.tomes.end());
            for (const Key tome : s.tomes) {
                TomeSource t = classifier.Of(tome);
                for (const VendorEntries& v : t.vendors) {
                    for (const Key e : v.entries) reach[e].insert(spell);
                    sold[spell].push_back(v);
                }
                out.tomes.emplace(tome, std::move(t));
            }
        }

        // A chest sells a spell for its plugin's set only through an entry half of whose spells are that plugin's
        std::unordered_map<Key, std::unordered_map<std::string, std::size_t>> perPlugin;   // entry -> plugin -> spells
        for (const auto& [e, spells] : reach) {
            for (const Key s : spells) ++perPlugin[e][pluginOf(s)];
        }
        std::unordered_map<Key, Chests> sells;
        std::set<Key> chestSet;
        for (const auto& [spell, vendors] : sold) {
            const std::string& plugin = pluginOf(spell);
            for (const VendorEntries& v : vendors) {
                for (const Key e : v.entries) {
                    if (2 * perPlugin[e][plugin] >= reach[e].size()) {
                        sells[spell].insert(v.chest);
                        chestSet.insert(v.chest);
                    }
                }
            }
        }
        // Sold through dedicated entries by more chests than a shop has: on wide sale, part of no shop
        for (auto& [_spell, chests] : sells) {
            if (chests.size() > kMaxShops + 1) chests.clear();
        }
        std::vector<Key> chestOrder(chestSet.begin(), chestSet.end());
        std::sort(chestOrder.begin(), chestOrder.end(),
            [&](Key a, Key b) { return std::tie(editorId(a), a) < std::tie(editorId(b), b); });
        std::map<std::string, std::vector<Key>> sets;   // plugin -> its vendor-sold tome spells
        for (const auto& [spell, _v] : sold) sets[pluginOf(spell)].push_back(spell);
        std::unordered_map<Key, std::string> shopOf;
        for (auto& [plugin, set] : sets) {
            std::sort(set.begin(), set.end());
            const bool anySold = std::any_of(set.begin(), set.end(), [&](Key s) {
                const auto it = sells.find(s);
                return it != sells.end() && !it->second.empty();
            });
            if (set.size() < kMinSet || !anySold ||
                std::find(kBasePlugins.begin(), kBasePlugins.end(), plugin) != kBasePlugins.end())
                continue;
            auto [n, chests] = BestCover(set, sells, chestOrder);
            if (n * kCoverDenominator < set.size() * kCoverNumerator) continue;
            for (const Key s : set) {
                const auto it = sells.find(s);
                if (it != sells.end() &&
                    std::any_of(chests.begin(), chests.end(), [&](Key c) { return it->second.contains(c); }))
                    shopOf[s] = plugin;
            }
            out.shops.push_back({ plugin, std::move(chests), set.size(), n });
        }

        for (auto& [spell, s] : out.spells) {
            std::set<std::pair<SourceClass, std::string>> routes;
            std::set<SourceClass> classes;
            bool first = true;
            for (const Key tome : s.tomes) {
                for (const Route& r : out.tomes[tome].routes) {
                    routes.emplace(r.cls, r.subtype);
                    classes.insert(r.cls);
                    if (first || r < s.best) s.best = r;
                    first = false;
                }
            }
            // Each vendor chest's stock of the spell: its most open entry (a tome held directly is open)
            std::map<Key, Gate> chests;
            for (const Key tome : s.tomes) {
                for (const VendorEntries& v : out.tomes[tome].vendors) {
                    Gate g = v.entries.empty() ? Gate::kOpen : Gate::kQuest;
                    for (const Key e : v.entries) g = std::min(g, GateOf(store, e));
                    const auto [it, added] = chests.emplace(v.chest, g);
                    if (!added) it->second = std::min(it->second, g);
                }
            }
            std::size_t open = 0;
            std::set<Gate> gated;
            for (const auto& [_chest, g] : chests) {
                if (g == Gate::kOpen) ++open;
                else gated.insert(g);
            }
            const bool standardLoot = std::any_of(routes.begin(), routes.end(), [](const auto& r) {
                return (r.first == SourceClass::kGeneric || r.first == SourceClass::kFaction) && r.second != "regional" &&
                       r.second != "vendors" && r.second != "specialty";
            });
            if (const auto shop = shopOf.find(spell); shop != shopOf.end()) s.shop = shop->second;
            if (classes.empty() || classes == std::set<SourceClass>{ SourceClass::kNone })
                s.segment = Segment::kUnreachable;
            else if (!s.shop.empty() && open <= kShopSale)
                s.segment = Segment::kSpellpackVendor;
            else if (gated.contains(Gate::kRitual))
                s.segment = Segment::kRitualQuest;
            else if (open >= kStandardVendors)
                s.segment = Segment::kStandardSale;
            else if (standardLoot)
                s.segment = Segment::kStandardLoot;
            else if (routes.contains({ SourceClass::kFaction, "regional" }))
                s.segment = Segment::kRegionalLoot;
            else if (open > 0 || gated.contains(Gate::kQuest))
                s.segment = Segment::kUniqueVendor;
            else if (classes.contains(SourceClass::kPlaced) || classes.contains(SourceClass::kEncounter) ||
                     classes.contains(SourceClass::kReward))
                s.segment = Segment::kUniqueReward;
            else if (classes.contains(SourceClass::kCrafted))
                s.segment = Segment::kCrafted;
            else
                s.segment = Segment::kReview;
        }
        return out;
    }
}
