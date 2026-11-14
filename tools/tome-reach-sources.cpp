// =============================================================================
// tome-reach-test --sources: the distribution model's classes and segments, dumped
// =============================================================================
//
//   tome-reach-test -i inputs.json --graph out.tsv --sources sources.tsv
//
// Writes, one fact per line (forms as plugin:LOCALID):
//   C <tome> <class> <subtype>               every route of a tome
//   D <spell> <class> <subtype>              a spell's most generic route
//   V <spell> <tome> <chest> <entries>       vendor chests above a tome, with the entries they hold (editor ids)
//   P <plugin> <chests> <sold>/<set>         specialty mod vendors
//   B <spell> <rule> <segment>               the segment, with the number of the Python bucket rule it is part of
// so a script can compare it with the Python model (distribution.py, shops.py) on the same load order.

#include "tomereach/TomeReachModel.h"

#include <chrono>
#include <cstdio>
#include <set>
#include <string>

namespace
{
    using namespace TomeReach;

    // shops.py RULES: 1 unreachable, 2 general sale, 3 specialty shop, 4 wide drops, 5 specific (reward and crafted
    // included), 6 specialty vendor only, 7 review
    int PythonRule(Segment s)
    {
        switch (s) {
        case Segment::kUnreachable: return 1;
        case Segment::kGeneralSale: return 2;
        case Segment::kSpecialtyModVendor: return 3;
        case Segment::kWideDrops: return 4;
        case Segment::kSpecificLoot:
        case Segment::kQuestReward:
        case Segment::kCraftedOnly: return 5;
        case Segment::kUncommonVendor: return 6;
        default: return 7;
        }
    }
}

int DumpSources(Model& model, const NameTable& names, const std::string& out)
{
    const auto started = std::chrono::steady_clock::now();
    const SpellStore& store = model.Store();
    const SpellSources sources = model.Classify();
    const auto done = std::chrono::steady_clock::now();
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    const auto text = [&](Key k) { return KeyText(names, k); };
    const auto edid = [&](Key k) {
        const auto it = store.editorIds.find(k);
        return it != store.editorIds.end() ? it->second : std::string();
    };
    for (const auto& [tome, t] : sources.tomes) {
        std::set<std::pair<std::string_view, std::string>> seen;
        for (const Route& r : t.routes) {
            if (seen.emplace(ClassName(r.cls), r.subtype).second)
                std::fprintf(f, "C\t%s\t%s\t%s\n", text(tome).c_str(), ClassName(r.cls).data(), r.subtype.c_str());
        }
    }
    std::size_t perSegment[9] = {};
    for (const auto& [spell, s] : sources.spells) {
        std::fprintf(f, "D\t%s\t%s\t%s\n", text(spell).c_str(), ClassName(s.best.cls).data(), s.best.subtype.c_str());
        std::fprintf(f, "B\t%s\t%d\t%s\n", text(spell).c_str(), PythonRule(s.segment), SegmentName(s.segment).data());
        ++perSegment[static_cast<std::size_t>(s.segment)];
        for (const Key tome : s.tomes) {
            for (const VendorEntries& v : sources.tomes.at(tome).vendors) {
                std::set<std::string> entries;
                for (const Key e : v.entries) entries.insert(edid(e));
                std::string via;
                for (const auto& e : entries) via += (via.empty() ? "" : ", ") + e;
                std::fprintf(f, "V\t%s\t%s\t%s\t%s\n", text(spell).c_str(), edid(tome).c_str(), edid(v.chest).c_str(), via.c_str());
            }
        }
    }
    for (const SpecialtyShop& shop : sources.shops) {
        std::string chests;
        for (const Key c : shop.chests) chests += (chests.empty() ? "" : ", ") + edid(c);
        std::fprintf(f, "P\t%s\t%s\t%zu/%zu\n", shop.plugin.c_str(), chests.c_str(), shop.sold, shop.set);
    }
    std::fclose(f);
    std::printf("sources: %zu spells, %zu specialty mod vendors in %lld ms;", sources.spells.size(), sources.shops.size(),
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(done - started).count()));
    for (std::size_t i = 0; i < 9; ++i) std::printf(" %s %zu%s", SegmentName(static_cast<Segment>(i)).data(), perSegment[i], i < 8 ? "," : "");
    std::printf(" -> %s\n", out.c_str());
    return 0;
}
