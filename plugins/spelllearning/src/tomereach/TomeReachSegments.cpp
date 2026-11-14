#include "tomereach/TomeReachSegments.h"
#include "AsciiText.h"

#include <algorithm>
#include <array>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace TomeReach
{
    namespace
    {
        constexpr std::size_t kMaxShops = 2;         // chests of a specialty mod vendor
        constexpr std::size_t kCoverNumerator = 3;   // ... that sell 3/4 of the set
        constexpr std::size_t kCoverDenominator = 4;
        constexpr std::size_t kMinSet = 3;           // vendor-sold tome spells a plugin needs to have a shop
        constexpr std::array<std::string_view, 5> kBasePlugins{ "skyrim.esm", "update.esm", "dawnguard.esm",
            "dragonborn.esm", "hearthfires.esm" };
        const std::string kNoName;

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
            for (std::size_t i = 0; i < chests.size(); ++i) consider({ chests[i] });
            for (std::size_t i = 0; i < chests.size(); ++i) {
                for (std::size_t j = i + 1; j < chests.size(); ++j) consider({ chests[i], chests[j] });
            }
            return best;
        }
    }

    std::string_view SegmentName(Segment segment)
    {
        constexpr std::array<std::string_view, 9> kNames{ "unreachable", "general sale", "specialty mod vendor",
            "wide drops", "specific loot", "quest reward", "crafted only", "uncommon vendor", "review" };
        return kNames[static_cast<std::size_t>(segment)];
    }

    bool SegmentSwitches::OnTree(Segment segment) const
    {
        switch (segment) {
        case Segment::kGeneralSale: return generalSale;
        case Segment::kSpecialtyModVendor: return specialtyModVendors;
        case Segment::kWideDrops: return wideDrops;
        case Segment::kSpecificLoot: return specificLoot;
        case Segment::kQuestReward: return questRewards;
        case Segment::kCraftedOnly: return craftedOnly;
        case Segment::kUncommonVendor: return uncommonVendors;
        default: return false;
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
        // Sold through dedicated entries by more chests than a shop has: general sale, part of no shop
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
            const auto local = [](const std::string& subtype) { return subtype == "local" || subtype == "regional"; };
            const auto any = [&](auto pred) { return std::any_of(routes.begin(), routes.end(), pred); };
            if (const auto shop = shopOf.find(spell); shop != shopOf.end()) s.shop = shop->second;
            if (classes.empty() || classes == std::set<SourceClass>{ SourceClass::kNone })
                s.segment = Segment::kUnreachable;
            else if (routes.contains({ SourceClass::kGeneric, "vendors" }) || routes.contains({ SourceClass::kGeneric, "loot" }))
                s.segment = Segment::kGeneralSale;
            else if (!s.shop.empty())
                s.segment = Segment::kSpecialtyModVendor;
            else if (any([&](const auto& r) {
                         return (r.first == SourceClass::kGeneric || r.first == SourceClass::kFaction) && !local(r.second);
                     }))
                s.segment = Segment::kWideDrops;
            else if (classes.contains(SourceClass::kPlaced) || classes.contains(SourceClass::kEncounter) ||
                     any([&](const auto& r) { return local(r.second); }))
                s.segment = Segment::kSpecificLoot;
            else if (classes.contains(SourceClass::kReward))
                s.segment = Segment::kQuestReward;
            else if (classes.contains(SourceClass::kCrafted))
                s.segment = Segment::kCraftedOnly;
            else if (classes == std::set<SourceClass>{ SourceClass::kVendor })
                s.segment = Segment::kUncommonVendor;
            else
                s.segment = Segment::kReview;
        }
        return out;
    }
}
