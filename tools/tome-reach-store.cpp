// =============================================================================
// tome-reach-test --store: the distribution model's spell store, dumped
// =============================================================================
//
//   tome-reach-test -i inputs.json --store out.tsv
//
// Reads the load order with ReadSpellStore (src/tomereach/TomeReachReader.cpp) and
// writes, one fact per line (forms as plugin:LOCALID, plugin names lower case):
//   E <child> <kind> <parent>   the direct parents of every tome, and of every leveled item list
//                               above one (kinds LVLI, CONT, NPC_, NPC_death, OTFT, QUST)
//   N <base> <placements>       references (not deleted) of every container, NPC, leveled NPC list,
//                               tome, scroll and staff
//   C <cell> <worldspace> <location> <interior> <editor id>   the cells those references are in
//   L <location> <parent> <editor id>
//   V <faction> <reference> <base>   vendor chests (FACT VENC)
// so a script can compare it with another reader of the same load order. The
// summary line gives the process's memory once the store is read (working set and
// its peak), for sizing the store.

#include "tomereach/TomeReachPlugin.h"
#include "tomereach/TomeReachReader.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace
{
    using TomeReach::Key;

    constexpr double kMegabyte = 1024.0 * 1024.0;

    std::string Text(const TomeReach::NameTable& names, Key key)
    {
        if (!key) return "-";
        char local[16];
        std::snprintf(local, sizeof(local), "%06X", TomeReach::LocalOf(key));
        return names.Name(TomeReach::PluginOf(key)) + ":" + local;
    }

    void PrintMemory(const char* when)
    {
        PROCESS_MEMORY_COUNTERS counters{};
        if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return;
        std::printf("memory %s: working set %.0f MB, peak %.0f MB, private %.0f MB\n", when,
            counters.WorkingSetSize / kMegabyte, counters.PeakWorkingSetSize / kMegabyte,
            counters.PagefileUsage / kMegabyte);
    }
}

int DumpStore(const std::vector<TomeReach::PluginFile>& loadOrder, const std::string& out)
{
    TomeReach::NameTable names;
    const auto store = TomeReach::ReadSpellStore(loadOrder, names);
    PrintMemory("after reading and filtering");
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    // Parents of every form, by the records the model reads
    std::map<Key, std::set<std::pair<std::string, Key>>> parents;
    for (const auto& [list, entries] : store.lists) {
        if (store.kinds.at(list) != TomeReach::Kind::kItemList) continue;
        for (const Key e : entries) parents[e].insert({ "LVLI", list });
    }
    for (const auto& [holder, items] : store.inventories) {
        if (store.kinds.at(holder) != TomeReach::Kind::kContainer) continue;
        for (const Key e : items) parents[e].insert({ "CONT", holder });
    }
    for (const auto& [npc, data] : store.npcs) {
        for (const Key e : data.items) parents[e].insert({ "NPC_", npc });
        if (data.deathItem) parents[data.deathItem].insert({ "NPC_death", npc });
        if (const auto o = store.inventories.find(data.outfit); data.outfit && o != store.inventories.end()) {
            for (const Key e : o->second) parents[e].insert({ "OTFT", npc });
        }
    }
    for (const auto& [quest, items] : store.questItems) {
        for (const Key e : items) parents[e].insert({ "QUST", quest });
    }
    std::set<Key> todo;
    for (const auto& [book, _spell] : store.bookSpells) todo.insert(book);
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
    for (const auto& [ref, r] : store.refs) {
        if (r.flags & TomeReach::kDeletedRecord) continue;
        ++placed[r.base];
        cells.insert(r.cell);
    }
    for (const auto& [base, n] : placed) std::fprintf(f, "N\t%s\t%zu\n", Text(names, base).c_str(), n);
    for (const Key c : cells) {
        const auto it = store.cells.find(c);
        if (it == store.cells.end()) {
            std::fprintf(f, "C\t%s\t?\n", Text(names, c).c_str());
            continue;
        }
        const auto& cell = it->second;
        std::fprintf(f, "C\t%s\t%s\t%s\t%d\t%s\n", Text(names, c).c_str(), Text(names, cell.worldspace).c_str(),
            Text(names, cell.location).c_str(), cell.interior ? 1 : 0, cell.editorId.c_str());
    }
    for (const auto& [loc, l] : store.locations) {
        std::fprintf(f, "L\t%s\t%s\t%s\n", Text(names, loc).c_str(), Text(names, l.parent).c_str(), l.editorId.c_str());
    }
    for (const auto& [faction, ref] : store.vendorChests) {
        const auto r = store.refs.find(ref);
        std::fprintf(f, "V\t%s\t%s\t%s\n", Text(names, faction).c_str(), Text(names, ref).c_str(),
            r != store.refs.end() ? Text(names, r->second.base).c_str() : "-");
    }
    std::fclose(f);
    std::printf("store: %zu records, %zu tomes, %zu refs kept, %zu cells, %zu locations, unreadable %zu, damaged %zu -> %s\n",
        store.kinds.size(), store.bookSpells.size(), store.refs.size(), store.cells.size(), store.locations.size(),
        store.unreadable.size(), store.damaged.size(), out.c_str());
    return 0;
}
