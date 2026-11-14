// =============================================================================
// tome-reach-test --world: the distribution model's record store, dumped
// =============================================================================
//
//   tome-reach-test -i inputs.json --world out.tsv
//
// Reads the load order with ReadWorld (src/tomereach/TomeReachWorld.cpp) and
// writes, one fact per line (forms as plugin:LOCALID, plugin names lower case):
//   E <child> <kind> <parent>   the direct parents of every tome, and of every leveled item list
//                               above one (kinds LVLI, CONT, NPC_, NPC_death, OTFT, QUST)
//   N <base> <placements>       references (not deleted) of every container, NPC, leveled NPC list,
//                               tome, scroll and staff
//   C <cell> <world> <location> <interior> <editor id>   the cells those references are in
//   L <location> <parent> <editor id>
//   V <faction> <reference> <base>   vendor chests (FACT VENC)
// so a script can compare it with another reader of the same load order.

#include "tomereach/TomeReachPlugin.h"
#include "tomereach/TomeReachWorld.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace
{
    using TomeReach::Key;

    std::string Text(const TomeReach::NameTable& names, Key key)
    {
        if (!key) return "-";
        char local[16];
        std::snprintf(local, sizeof(local), "%06X", TomeReach::LocalOf(key));
        return names.Name(TomeReach::PluginOf(key)) + ":" + local;
    }
}

int DumpWorld(const std::vector<TomeReach::PluginFile>& loadOrder, const std::string& out)
{
    TomeReach::NameTable names;
    const auto world = TomeReach::ReadWorld(loadOrder, names);
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    // Parents of every form, by the records the model reads
    std::map<Key, std::set<std::pair<std::string, Key>>> parents;
    for (const auto& [list, entries] : world.lists) {
        if (world.kinds.at(list) != TomeReach::Kind::kItemList) continue;
        for (const Key e : entries) parents[e].insert({ "LVLI", list });
    }
    for (const auto& [holder, items] : world.inventories) {
        if (world.kinds.at(holder) != TomeReach::Kind::kContainer) continue;
        for (const Key e : items) parents[e].insert({ "CONT", holder });
    }
    for (const auto& [npc, data] : world.npcs) {
        for (const Key e : data.items) parents[e].insert({ "NPC_", npc });
        if (data.deathItem) parents[data.deathItem].insert({ "NPC_death", npc });
        if (const auto o = world.inventories.find(data.outfit); data.outfit && o != world.inventories.end()) {
            for (const Key e : o->second) parents[e].insert({ "OTFT", npc });
        }
    }
    for (const auto& [quest, items] : world.questItems) {
        for (const Key e : items) parents[e].insert({ "QUST", quest });
    }
    std::set<Key> todo;
    for (const auto& [book, _spell] : world.bookSpells) todo.insert(book);
    std::set<Key> seen = todo;
    while (!todo.empty()) {
        const Key x = *todo.begin();
        todo.erase(todo.begin());
        const auto p = parents.find(x);
        if (p == parents.end()) continue;
        for (const auto& [kind, parent] : p->second) {
            std::fprintf(f, "E\t%s\t%s\t%s\n", Text(names, x).c_str(), kind.c_str(), Text(names, parent).c_str());
            if (kind == "LVLI" && seen.insert(parent).second) todo.insert(parent);
        }
    }
    std::map<Key, std::size_t> placed;
    std::set<Key> cells;
    for (const auto& [ref, r] : world.refs) {
        if (r.flags & TomeReach::kDeletedRecord) continue;
        ++placed[r.base];
        cells.insert(r.cell);
    }
    for (const auto& [base, n] : placed) std::fprintf(f, "N\t%s\t%zu\n", Text(names, base).c_str(), n);
    for (const Key c : cells) {
        const auto it = world.cells.find(c);
        if (it == world.cells.end()) {
            std::fprintf(f, "C\t%s\t?\n", Text(names, c).c_str());
            continue;
        }
        const auto& cell = it->second;
        std::fprintf(f, "C\t%s\t%s\t%s\t%d\t%s\n", Text(names, c).c_str(), Text(names, cell.world).c_str(),
            Text(names, cell.location).c_str(), cell.interior ? 1 : 0, cell.editorId.c_str());
    }
    for (const auto& [loc, l] : world.locations) {
        std::fprintf(f, "L\t%s\t%s\t%s\n", Text(names, loc).c_str(), Text(names, l.parent).c_str(), l.editorId.c_str());
    }
    for (const auto& [faction, ref] : world.vendorChests) {
        const auto r = world.refs.find(ref);
        std::fprintf(f, "V\t%s\t%s\t%s\n", Text(names, faction).c_str(), Text(names, ref).c_str(),
            r != world.refs.end() ? Text(names, r->second.base).c_str() : "-");
    }
    std::fclose(f);
    std::printf("world: %zu records, %zu tomes, %zu refs kept, %zu cells, %zu locations, unreadable %zu, damaged %zu -> %s\n",
        world.kinds.size(), world.bookSpells.size(), world.refs.size(), world.cells.size(), world.locations.size(),
        world.unreadable.size(), world.damaged.size(), out.c_str());
    return 0;
}
