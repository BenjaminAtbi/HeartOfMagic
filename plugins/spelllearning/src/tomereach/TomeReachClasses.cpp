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
        constexpr std::size_t kGenericVendors = 5;      // vendor chests that make a generic vendors route
        // Loot (containers and NPCs, by placed references) is standard at this many sites or regions, regional at
        // two sites or more, one site's loot below
        constexpr std::size_t kStandardSites = 20;
        constexpr std::size_t kStandardRegions = 5;
        constexpr std::uint32_t kUniqueNpc = 0x20;      // ACBS flag
        constexpr std::size_t kDetailItems = 4;         // names listed in a detail
        constexpr std::uint8_t kAlwaysNone = 100;       // a list's chance of nothing that closes it until a global opens it
        constexpr std::string_view kRitualGlobal = "mgritual";   // the College's master ritual globals
        constexpr std::string_view kSkillGlobal = "pc";          // vanilla's skill thresholds: ordinary stock
        constexpr std::string_view kSpawned = kSpawnedRegion;
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

    GatedKind GatedListOf(const SpellStore& store, Key list)
    {
        const auto gated = store.gatedLists.find(list);
        if (gated == store.gatedLists.end() || gated->second.chanceNone < kAlwaysNone) return GatedKind::kOpen;
        const auto global = store.globalIds.find(gated->second.global);
        const std::string id = AsciiText::ToLower(global != store.globalIds.end() ? global->second : kNoName);
        if (id.starts_with(kSkillGlobal)) return GatedKind::kOpen;
        return id.starts_with(kRitualGlobal) ? GatedKind::kRitual : GatedKind::kQuest;
    }

    std::unordered_map<Key, GatedKind> Classifier::PathGates(Key tome) const
    {
        std::unordered_map<Key, GatedKind> best{ { tome, GatedKind::kOpen } };
        std::vector<Key> todo{ tome };
        while (!todo.empty()) {
            const Key x = todo.back();
            todo.pop_back();
            for (const Key list : m_graph.ListsHolding(x)) {
                const GatedKind cand = std::max(best.at(x), GatedListOf(m_store, list));
                const auto it = best.find(list);
                if (it != best.end() && cand >= it->second) continue;
                best[list] = cand;
                todo.push_back(list);
            }
        }
        return best;
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
            const Placements& p = m_sites.OfNpc(n, true);
            if (!p.count) continue;
            placedNpcs.push_back(n);
            npcPlaces.Add(p);
        }
        std::size_t npcRefs = 0;
        for (const Key n : placedNpcs) npcRefs += m_sites.OfNpc(n, true).count;
        // A spawn's stand-in site only counts when nothing else does
        const auto real = [](const auto& entry) { return entry.first.region != kSpawned; };
        if (std::any_of(cont.sites.begin(), cont.sites.end(), real) ||
            std::any_of(npcPlaces.sites.begin(), npcPlaces.sites.end(), real))
            std::erase_if(npcPlaces.sites, [&](const auto& entry) { return !real(entry); });
        std::set<RegionSite> sites;
        std::map<std::string, std::size_t> regions;   // region -> placements
        for (const auto& [where, n] : cont.sites) {
            sites.insert(where);
            regions[where.region] += n;
        }
        for (const auto& [where, n] : npcPlaces.sites) {
            sites.insert(where);
            regions[where.region] += n;
        }

        if (vendors.size() >= kGenericVendors) {
            std::vector<std::string> vendorIds;
            for (const Key v : vendors) vendorIds.push_back(EditorId(v));
            std::sort(vendorIds.begin(), vendorIds.end());
            out.push_back({ SourceClass::kGeneric, "vendors", std::to_string(vendors.size()) + " vendors: " + Join(vendorIds, kDetailItems) });
        }
        if (sites.size() >= 2) {
            std::vector<std::pair<std::size_t, std::string>> top;
            for (const auto& [r, n] : regions) top.emplace_back(n, r);
            std::stable_sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            std::vector<std::string> regionText;
            for (const auto& [n, r] : top) regionText.push_back(r + " " + std::to_string(n));
            const std::string what = std::to_string(npcRefs) + " NPCs placed (" + std::to_string(placedNpcs.size()) +
                                     " bases), " + std::to_string(cont.count) + " containers; " +
                                     std::to_string(sites.size()) + " sites in " + std::to_string(regions.size()) +
                                     " regions: " + Join(regionText, 3);
            if (sites.size() >= kStandardSites || regions.size() >= kStandardRegions)
                out.push_back({ SourceClass::kGeneric, "loot", what });
            else
                out.push_back({ SourceClass::kFaction, "regional", what });
        }
        if (!vendors.empty() && vendors.size() < kGenericVendors) {
            std::vector<std::string> where;
            for (std::size_t i = 0; i < vendors.size() && i < kDetailItems; ++i) {
                const auto names = SiteNames(m_sites.OfBase(vendors[i]));
                where.push_back(EditorId(vendors[i]) + " @ " + (names.empty() ? std::string("unplaced") : Join(names, names.size())));
            }
            out.push_back({ SourceClass::kVendor, "specialty", Join(where, where.size()) });
        }
        if (sites.size() == 1) {
            std::vector<std::string> bosses;
            for (const Key n : placedNpcs) {
                const auto npc = m_store.npcs.find(n);
                if ((npc != m_store.npcs.end() && (npc->second.flags & kUniqueNpc)) || HasBoss(EditorId(n))) bosses.push_back(EditorId(n));
            }
            const bool boss = !bosses.empty() ||
                std::any_of(containers.begin(), containers.end(), [&](Key c) { return HasBoss(EditorId(c)); });
            out.push_back({ SourceClass::kEncounter, boss ? "boss" : "loot",
                "1 site(s): " + sites.begin()->site + (bosses.empty() ? "" : " (" + Join(bosses, 3) + ")") });
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
            const auto gates = PathGates(tome);
            for (auto& [v, held] : entries) {
                GatedKind gate = GatedKind::kQuest;   // an entry no path reaches: closed
                for (const Key e : held) {
                    if (const auto g = gates.find(e); g != gates.end()) gate = std::min(gate, g->second);
                }
                out.vendors.push_back({ v, std::move(held), gate });
            }
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
            for (const Reward& w : rewards) who.push_back(m_graph.HolderLabel(w.holder) + (w.alias ? " (alias)" : ""));
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
