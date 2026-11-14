// =============================================================================
// tome-reach-test --graph: the distribution model's reach graph and sites, dumped
// =============================================================================
//
//   tome-reach-test -i inputs.json --graph out.tsv [--records-only]
//
// Builds the spell store, the reach graph and the sites (src/tomereach/) and
// writes, one fact per line (forms as plugin:LOCALID):
//   R <tome> <CONT|NPC_> <root>       the containers and NPCs above every tome through leveled lists
//   R <tome> SPID <config>|<item|deathitem>|<chance>|<class>|<subtype>   SPID lines above it
//   T <tome> <vendors> <containers> <container placements> <container sites> <placed NPCs>
//     <NPC sites> <sites> <tome placements> <rewards> <recipes> <enabled recipes> <SPID lines> <route>
//                                     what the classes read; route 1 when the tome has any route
// The configs (SPID, SkyPatcher) and scripts come from the input's data folders;
// --records-only leaves them out (stage 2).
//   S <base> <region> <site> <placements>   per (region, site), for every tome, root and NPC base
//   U <spell>                         spells whose every tome has no route (tomeUnreachable)
// so a script can compare it with the Python model on the same load order. --sources also writes the
// classes and segments (tome-reach-sources.cpp).

#include "tomereach/TomeReachModel.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace
{
    using namespace TomeReach;

    struct TomeFacts
    {
        std::size_t vendors = 0, containers = 0, placedNpcs = 0, rewards = 0, recipes = 0, enabledRecipes = 0, spid = 0;
        Placements containerPlaces, npcPlaces, tomePlaces;
        std::size_t sites = 0;

        [[nodiscard]] bool Route() const
        {
            return vendors || sites || tomePlaces.count || rewards || enabledRecipes || spid;
        }
    };
}

int DumpSources(Model& model, const NameTable& names, const std::string& out);   // tome-reach-sources.cpp

int DumpGraph(const std::vector<PluginFile>& loadOrder, const std::vector<std::filesystem::path>& dataDirs,
    const std::string& out, const std::string& sourcesOut)
{
    NameTable names;
    Model model(loadOrder, names, dataDirs);
    const auto built = std::chrono::steady_clock::now();
    const SpellStore& store = model.Store();
    const ReachGraph& graph = model.Graph();
    Sites& sites = model.Places();
    const Distribution* distribution = model.Configs();
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    const auto text = [&](Key k) { return KeyText(names, k); };

    std::set<Key> need;
    std::set<Key> expanded;   // NPCs whose bases are in `need` already
    std::map<Key, bool> routeOf;
    for (const auto& [tome, _spell] : std::map<Key, Key>(store.bookSpells.begin(), store.bookSpells.end())) {
        const Roots r = graph.RootsOf(tome);
        TomeFacts t;
        std::vector<Key> containers;
        need.insert(tome);
        for (const Root& root : r.roots) {
            if (root.kind == RootKind::kSpid) {
                const SpidLine& line = distribution->spid[root.form];
                const SpidClass c = ClassOf(line);
                std::fprintf(f, "R\t%s\tSPID\t%s|%s|%s|%.*s|%.*s\n", text(tome).c_str(), line.source.c_str(),
                    line.deathItem ? "deathitem" : "item", line.chance.c_str(), static_cast<int>(c.cls.size()), c.cls.data(),
                    static_cast<int>(c.subtype.size()), c.subtype.data());
                ++t.spid;
                continue;
            }
            std::fprintf(f, "R\t%s\t%s\t%s\n", text(tome).c_str(), root.kind == RootKind::kContainer ? "CONT" : "NPC_",
                text(root.form).c_str());
            if (root.kind == RootKind::kContainer) {
                need.insert(root.form);
                if (graph.IsVendor(root.form)) ++t.vendors;
                else containers.push_back(root.form);
                continue;
            }
            if (expanded.insert(root.form).second) {
                for (const Key b : graph.NpcBases(root.form)) need.insert(b);
            }
            const Placements& p = sites.OfNpc(root.form);
            if (p.count) {
                ++t.placedNpcs;
                t.npcPlaces.Add(p);
            }
        }
        t.containers = containers.size();
        t.containerPlaces = sites.Of(containers);
        std::set<RegionSite> all;
        for (const auto& [where, _n] : t.containerPlaces.sites) all.insert(where);
        for (const auto& [where, _n] : t.npcPlaces.sites) all.insert(where);
        t.sites = all.size();
        t.tomePlaces = sites.OfBase(tome);
        std::set<Reward> rewards;
        for (const Key list : r.lists) {
            for (const Reward& w : graph.RewardsOf(list)) rewards.insert(w);
        }
        t.rewards = rewards.size();
        for (const Key recipe : graph.RecipesOf(tome)) {
            ++t.recipes;
            if (!graph.RecipeDisabled(recipe)) ++t.enabledRecipes;
        }
        std::fprintf(f, "T\t%s\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%d\n", text(tome).c_str(),
            t.vendors, t.containers, t.containerPlaces.count, t.containerPlaces.sites.size(), t.placedNpcs,
            t.npcPlaces.sites.size(), t.sites, t.tomePlaces.count, t.rewards, t.recipes, t.enabledRecipes, t.spid,
            t.Route() ? 1 : 0);
        routeOf[tome] = t.Route();
    }
    for (const Key base : need) {
        for (const auto& [where, n] : sites.OfBase(base).sites) {
            std::fprintf(f, "S\t%s\t%s\t%s\t%zu\n", text(base).c_str(), where.region.c_str(), where.site.c_str(), n);
        }
    }
    std::map<Key, bool> spellReached;
    for (const auto& [tome, spell] : store.bookSpells) spellReached[spell] = spellReached[spell] || routeOf[tome];
    std::size_t unreachable = 0;
    for (const auto& [spell, reached] : spellReached) {
        if (reached) continue;
        ++unreachable;
        std::fprintf(f, "U\t%s\n", text(spell).c_str());
    }
    std::fclose(f);
    const auto ms = [](auto a, auto b) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count());
    };
    std::printf("configs: %zu files, %zu SPID lines, %zu SkyPatcher adds, %zu archives with scripts or strings; "
                "%zu script holders, %zu scripts read, %zu not found\n",
        model.ConfigFiles().files, distribution ? distribution->spid.size() : 0,
        distribution ? distribution->listAdds.size() : 0, model.Data() ? model.Data()->ArchiveCount() : 0,
        store.holders.size(), graph.ScriptsRead(), graph.ScriptsMissing());
    std::printf("graph: %zu tomes, %zu spells, %zu unreachable, %zu bases placed; data files and configs %lld ms, "
                "store %lld ms, graph (scripts included) %lld ms, sites and dump %lld ms -> %s\n",
        store.bookSpells.size(), spellReached.size(), unreachable, need.size(), model.Times().configs,
        model.Times().store, model.Times().graph, ms(built, std::chrono::steady_clock::now()), out.c_str());
    if (!sourcesOut.empty()) return DumpSources(model, names, sourcesOut);
    return 0;
}
