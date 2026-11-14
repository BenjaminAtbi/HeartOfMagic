#include "tomereach/TomeReachClasses.h"
#include "AsciiText.h"

#include <algorithm>
#include <array>
#include <set>
#include <tuple>

namespace TomeReach
{
    namespace
    {
        constexpr std::size_t kGenericVendors = 5;      // vendor chests that make a shared list generic
        constexpr std::size_t kGenericRegions = 3;      // regions of containers or NPCs that make it generic
        constexpr std::size_t kGenericNpcs = 100;       // placed NPCs of mixed factions that make it generic
        constexpr std::size_t kFactionReach = 3;        // NPCs and containers, and sites, of a faction route
        constexpr std::size_t kWideSites = 20;          // a faction's drops below this many sites are local
        constexpr std::uint32_t kUniqueNpc = 0x20;      // ACBS flag
        constexpr std::size_t kDetailItems = 4;         // names listed in a detail
        // Factions too broad to name a group of NPCs
        constexpr std::array<std::string_view, 7> kBroadFactions{ "CreatureFaction", "PredatorFaction", "PreyFaction",
            "CurrentFollowerFaction", "PotentialFollowerFaction", "PotentialMarriageFaction", "DunPlayerAllyFaction" };
        const std::string kNoName;

        bool HasBoss(const std::string& editorId) { return AsciiText::ToLower(editorId).find("boss") != std::string::npos; }

        std::string Join(const std::vector<std::string>& items, std::size_t most, std::string_view separator = ", ")
        {
            std::string out;
            for (std::size_t i = 0; i < items.size() && i < most; ++i) {
                if (i) out += separator;
                out += items[i];
            }
            if (items.size() > most) out += " ...";
            return out;
        }

        std::vector<std::string> SiteNames(const Placements& p)
        {
            std::set<std::string> names;
            for (const auto& [where, _n] : p.sites) names.insert(where.site);
            return { names.begin(), names.end() };
        }
    }

    std::string_view ClassName(SourceClass cls)
    {
        constexpr std::array<std::string_view, 9> kNames{ "generic", "faction", "vendor", "encounter", "placed", "reward",
            "crafted", "other", "none" };
        return kNames[static_cast<std::size_t>(cls)];
    }

    bool Route::operator<(const Route& other) const
    {
        return std::tie(cls, subtype, detail) < std::tie(other.cls, other.subtype, other.detail);
    }

    Classifier::Classifier(const SpellStore& store, const ReachGraph& graph, Sites& sites, const Distribution* distribution) :
        m_store(store), m_graph(graph), m_sites(sites), m_distribution(distribution)
    {}

    const std::string& Classifier::EditorId(Key form) const
    {
        if (const auto it = m_store.editorIds.find(form); it != m_store.editorIds.end()) return it->second;
        if (const auto it = m_store.keywordIds.find(form); it != m_store.keywordIds.end()) return it->second;
        return kNoName;
    }

    const std::vector<Route>& Classifier::FromRoots(const std::vector<Key>& vendors, const std::vector<Key>& containers,
        const std::vector<Key>& npcs)
    {
        std::vector<Key> key = vendors;
        key.insert(key.end(), containers.begin(), containers.end());
        key.insert(key.end(), npcs.begin(), npcs.end());
        std::sort(key.begin(), key.end());
        if (const auto it = m_memo.find(key); it != m_memo.end()) return it->second;

        std::vector<Route> out;
        const Placements cont = m_sites.Of(containers);
        Placements npcPlaces;
        std::vector<Key> placedNpcs;
        for (const Key n : npcs) {
            const Placements& p = m_sites.OfNpc(n);
            if (!p.count) continue;
            placedNpcs.push_back(n);
            npcPlaces.Add(p);
        }
        std::set<RegionSite> sites;
        for (const auto& [where, _n] : cont.sites) sites.insert(where);
        for (const auto& [where, _n] : npcPlaces.sites) sites.insert(where);
        // The faction most placed NPCs share (broad ones left out); a faction route when half of them do
        std::map<Key, std::size_t> factions;
        for (const Key n : placedNpcs) {
            const auto npc = m_store.npcs.find(n);
            if (npc == m_store.npcs.end()) continue;
            const std::set<Key> own(npc->second.factions.begin(), npc->second.factions.end());
            for (const Key f : own) {
                const std::string& id = EditorId(f);
                if (std::find(kBroadFactions.begin(), kBroadFactions.end(), id) == kBroadFactions.end()) ++factions[f];
            }
        }
        Key topFaction = 0;
        std::size_t topCount = 0;
        for (const auto& [f, n] : factions) {
            if (n > topCount) std::tie(topFaction, topCount) = std::tie(f, n);
        }
        const bool isFaction = topCount && 2 * topCount >= placedNpcs.size();

        std::vector<std::string> vendorIds;
        for (const Key v : vendors) vendorIds.push_back(EditorId(v));
        std::sort(vendorIds.begin(), vendorIds.end());
        if (vendors.size() >= kGenericVendors)
            out.push_back({ SourceClass::kGeneric, "vendors", std::to_string(vendors.size()) + " vendors: " + Join(vendorIds, kDetailItems) });
        std::set<std::string> contRegions, npcRegions;
        for (const auto& [where, _n] : cont.sites) contRegions.insert(where.region);
        for (const auto& [where, _n] : npcPlaces.sites) npcRegions.insert(where.region);
        if (contRegions.size() >= kGenericRegions)
            out.push_back({ SourceClass::kGeneric, "loot",
                std::to_string(cont.count) + " containers in " + std::to_string(contRegions.size()) + " regions" });
        if (placedNpcs.size() >= kGenericNpcs && npcRegions.size() >= kGenericRegions && !isFaction)
            out.push_back({ SourceClass::kGeneric, "npcs",
                std::to_string(placedNpcs.size()) + " NPCs in " + std::to_string(npcRegions.size()) + " regions" });
        if (out.empty() && placedNpcs.size() + cont.count >= kFactionReach && sites.size() >= kFactionReach) {
            const std::string what = isFaction
                ? EditorId(topFaction) + " (" + std::to_string(topCount) + " of " + std::to_string(placedNpcs.size()) + " NPCs)"
                : std::to_string(placedNpcs.size()) + " NPCs, " + std::to_string(cont.count) + " containers";
            std::map<std::string, std::size_t> regions;
            for (const auto& [where, n] : cont.sites) regions[where.region] += n;
            for (const auto& [where, n] : npcPlaces.sites) regions[where.region] += n;
            std::vector<std::pair<std::size_t, std::string>> top;
            for (const auto& [r, n] : regions) top.emplace_back(n, r);
            std::stable_sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            std::vector<std::string> regionText;
            for (const auto& [n, r] : top) regionText.push_back(r + " " + std::to_string(n));
            out.push_back({ SourceClass::kFaction,
                isFaction ? (sites.size() >= kWideSites ? "faction" : "local") : "regional",
                what + "; " + std::to_string(sites.size()) + " sites; regions " + Join(regionText, 3) });
        }
        const auto has = [&](std::initializer_list<SourceClass> classes) {
            return std::any_of(out.begin(), out.end(), [&](const Route& r) {
                return std::find(classes.begin(), classes.end(), r.cls) != classes.end();
            });
        };
        if (!vendors.empty() && !has({ SourceClass::kGeneric })) {
            std::vector<std::string> where;
            for (std::size_t i = 0; i < vendors.size() && i < kDetailItems; ++i) {
                const auto names = SiteNames(m_sites.OfBase(vendors[i]));
                where.push_back(EditorId(vendors[i]) + " @ " + (names.empty() ? std::string("unplaced") : Join(names, names.size())));
            }
            out.push_back({ SourceClass::kVendor, "specialty", Join(where, where.size()) });
        }
        if (!has({ SourceClass::kGeneric, SourceClass::kFaction }) && !sites.empty()) {
            std::vector<std::string> bosses;
            for (const Key n : placedNpcs) {
                const auto npc = m_store.npcs.find(n);
                if ((npc != m_store.npcs.end() && (npc->second.flags & kUniqueNpc)) || HasBoss(EditorId(n))) bosses.push_back(EditorId(n));
            }
            const bool boss = !bosses.empty() ||
                std::any_of(containers.begin(), containers.end(), [&](Key c) { return HasBoss(EditorId(c)); });
            std::set<std::string> siteNames;
            for (const auto& s : sites) siteNames.insert(s.site);
            out.push_back({ SourceClass::kEncounter, boss ? "boss" : "loot",
                std::to_string(sites.size()) + " site(s): " + Join({ siteNames.begin(), siteNames.end() }, 3) +
                    (bosses.empty() ? "" : " (" + Join(bosses, 3) + ")") });
        }
        return m_memo.emplace(std::move(key), std::move(out)).first->second;
    }

    TomeSource Classifier::Of(Key tome)
    {
        TomeSource out;
        const Roots r = m_graph.RootsOf(tome);
        std::vector<Key> vendors, containers, npcs;
        std::map<std::pair<std::string_view, std::string_view>, std::vector<std::string>> spid;
        for (const Root& root : r.roots) {
            if (root.kind == RootKind::kSpid) {
                if (!m_distribution) continue;
                const SpidLine& line = m_distribution->spid[root.form];
                const SpidClass c = ClassOf(line);
                spid[{ c.cls, c.subtype }].push_back("SPID " + std::string(line.deathItem ? "deathitem" : "item") + " " +
                                                     line.chance + "% [" + line.source + "]");
            } else if (root.kind == RootKind::kNpc) {
                npcs.push_back(root.form);
            } else if (m_graph.IsVendor(root.form)) {
                vendors.push_back(root.form);
            } else {
                containers.push_back(root.form);
            }
        }
        out.routes = FromRoots(vendors, containers, npcs);
        if (!vendors.empty()) {
            // The entry each vendor chest holds: the tome or a leveled list above it
            std::vector<std::string> via;
            std::map<Key, std::vector<Key>> entries;   // vendor chest -> the lists (or the tome) it holds
            for (const Key v : vendors) entries[v];
            for (const Key x : r.lists) {
                for (const Key holder : m_graph.HoldersOf(x)) {
                    const auto it = entries.find(holder);
                    if (it == entries.end() || (!it->second.empty() && it->second.back() == x)) continue;
                    it->second.push_back(x);
                    via.push_back(EditorId(x));
                }
            }
            for (auto& [v, held] : entries) out.vendors.push_back({ v, std::move(held) });
            std::sort(via.begin(), via.end());
            via.erase(std::unique(via.begin(), via.end()), via.end());
            for (Route& route : out.routes) {
                if (route.subtype == "vendors" || route.subtype == "specialty") route.detail += " | via " + Join(via, kDetailItems);
            }
            std::sort(out.vendors.begin(), out.vendors.end(), [&](const VendorEntries& a, const VendorEntries& b) {
                return std::tie(EditorId(a.chest), a.chest) < std::tie(EditorId(b.chest), b.chest);
            });
        }
        for (auto& [cls, details] : spid) {
            const auto c = cls.first == "generic" ? SourceClass::kGeneric
                         : cls.first == "faction" ? SourceClass::kFaction
                         : cls.first == "encounter" ? SourceClass::kEncounter
                                                    : SourceClass::kOther;
            out.routes.push_back({ c, std::string(cls.second), Join(details, details.size(), "; ") });
        }
        if (const Placements& placed = m_sites.OfBase(tome); placed.count) {
            out.routes.push_back({ SourceClass::kPlaced, "world",
                std::to_string(placed.count) + " placed: " + Join(SiteNames(placed), kDetailItems) });
        }
        std::set<Reward> rewards;
        for (const Key list : r.lists) {
            for (const Reward& w : m_graph.RewardsOf(list)) rewards.insert(w);
        }
        if (!rewards.empty()) {
            std::vector<std::string> who;
            for (const Reward& w : rewards) who.push_back(EditorId(w.holder) + (w.alias ? " (alias)" : ""));
            std::sort(who.begin(), who.end());
            out.routes.push_back({ SourceClass::kReward, "script", Join(who, 3, "; ") });
        }
        for (const Key recipe : m_graph.RecipesOf(tome)) {
            if (m_graph.RecipeDisabled(recipe)) {
                out.routes.push_back({ SourceClass::kNone, "disabled recipe", EditorId(recipe) });
                continue;
            }
            const auto it = m_store.recipes.find(recipe);
            const Key bench = it != m_store.recipes.end() ? it->second.bench : 0;
            std::vector<std::string> parts;
            if (it != m_store.recipes.end()) {
                for (const Key c : it->second.components) parts.push_back(EditorId(c));
            }
            out.routes.push_back({ SourceClass::kCrafted, bench ? EditorId(bench) : "?",
                EditorId(recipe) + ": " + Join(parts, parts.size()) });
        }
        if (out.routes.empty()) out.routes.push_back({ SourceClass::kNone, "", "no route traced" });
        return out;
    }
}
