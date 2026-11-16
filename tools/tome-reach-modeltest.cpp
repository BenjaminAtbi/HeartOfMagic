// =============================================================================
// tome-reach-test --self-test  -  the distribution model's rules
// =============================================================================
//
// Checks the reach graph, sites and classes on a hand-built spell store (no
// plugin files): the noise and test-cell names, effective NPC inventories
// (template flags, default outfits), the bases that place an inventory,
// SkyPatcher removals, quest aliases that spawn an NPC (a stand-in site when
// nothing places it) and vendor gates along the most open path. Called from
// tome-reach-selftest.cpp.

#include "tomereach/TomeReachClasses.h"
#include "tomereach/TomeReachGraph.h"
#include "tomereach/TomeReachSites.h"

#include <algorithm>
#include <cstdio>

namespace
{
    using namespace TomeReach;

    constexpr std::uint16_t kUseInventory = 0x100;   // ACBS template flag
    constexpr std::uint8_t kAlwaysNone = 100;

    int g_passed = 0;
    int g_failed = 0;

    void Check(bool ok, const char* what)
    {
        (ok ? g_passed : g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }

    template <class T>
    bool Has(const std::vector<T>& v, const T& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

    void NameRules()
    {
        Check(IsNoiseEditorId("QATestChest") && IsNoiseEditorId("QA_Chest") && !IsNoiseEditorId("QAlderim"),
            "noise: QA as written and not followed by a lower case letter (Vominheim's QAlderim is real)");
        Check(!IsNoiseEditorId("qaChest") && IsNoiseEditorId("zzzTestBook") && IsNoiseEditorId("MyDebugChest"),
            "noise: qa in lower case is not; zzzTest first or Debug anywhere is");
        Check(IsTestCellName("QASmoke") && IsTestCellName("QA_Cell") && !IsTestCellName("QAlderim") &&
                  !IsTestCellName("qasmoke") && IsTestCellName("WarehouseTESTCELL"),
            "test cells: QA and a capital or underscore, as written; TestCell anywhere");
    }
}

int RunModelChecks(int& passed)
{
    NameRules();
    NameTable names;
    const std::uint32_t plugin = names.Id("Model.esm");
    const auto k = [&](std::uint32_t id) { return MakeKey(plugin, id); };
    SpellStore store;
    const Key tome = k(0x800), open = k(0x801);
    const Key list = k(0x810), removed = k(0x811), gated = k(0x812), outer = k(0x813), glob = k(0x814);
    const Key parent = k(0x820), child = k(0x821), ownChild = k(0x822), outfitted = k(0x823);
    const Key npcList = k(0x830), outfit = k(0x840), quest = k(0x850), chest = k(0x860), faction = k(0x861);
    const Key chestRef = k(0x870);
    for (const Key b : { tome, open }) store.kinds[b] = Kind::kBook;
    for (const Key l : { list, removed, gated, outer }) store.kinds[l] = Kind::kItemList;
    for (const Key n : { parent, child, ownChild, outfitted }) store.kinds[n] = Kind::kNpc;
    store.kinds[npcList] = Kind::kNpcList;
    store.kinds[outfit] = Kind::kOutfit;
    store.kinds[quest] = Kind::kQuest;
    store.kinds[chest] = Kind::kContainer;
    store.kinds[faction] = Kind::kFaction;
    store.editorIds[quest] = "SpawnQuest";
    store.lists[list] = { tome };
    store.lists[removed] = { tome };
    store.lists[npcList] = { child };
    store.npcs[parent].items = { tome };
    store.npcs[child] = Npc{ .templ = parent, .templateFlags = kUseInventory };
    store.npcs[ownChild] = Npc{ .templ = parent };
    store.npcs[outfitted] = Npc{ .outfit = outfit };
    store.inventories[outfit] = { tome };
    store.questCreates[quest] = { child };

    Distribution distribution;
    distribution.listAdds.push_back({ tome, { removed }, true });
    const ReachGraph graph(store, &distribution);
    const Roots r = graph.RootsOf(tome);
    const auto rootOf = [&](Key n) { return Has(r.roots, Root{ RootKind::kNpc, n }); };
    Check(rootOf(parent) && rootOf(child) && !rootOf(ownChild),
        "NPC inventory: a template child with Use Inventory carries its template's, one without its own");
    Check(rootOf(outfitted), "NPC inventory: the default outfit's items are looted from the body");
    Check(Has(r.lists, list) && !Has(r.lists, removed), "SkyPatcher removeFromLLs takes the tome out of that list");
    const auto bases = graph.NpcBases(parent, true);
    Check(bases == std::vector<Key>{ parent } && graph.NpcBases(parent).size() == 4,
        "inventory bases: template children are left out (they carry their own effective inventory)");
    const auto& spawners = graph.SpawnersOf(child);
    Check(spawners.size() == 1 && spawners[0].kind == SpawnKind::kAlias && spawners[0].label == "QUST SpawnQuest (alias)",
        "spawners: a quest alias that creates an NPC");

    Sites sites(store, names, graph);
    const Placements& spawned = sites.OfNpc(child, true);
    Check(spawned.count == 1 && spawned.sites.begin()->first.region == kSpawnedRegion,
        "sites: a spawned NPC placed nowhere stands in as a site of its own");

    // Vendor gates: the chest holds `outer`, which holds the ritual-gated list above `tome`, and `open` directly
    SpellStore shop = store;
    shop.kinds[glob] = Kind::kFaction;   // any kind: only its editor id is read
    shop.globalIds[glob] = "MGRitualRestoration";
    shop.gatedLists[gated] = GatedList{ kAlwaysNone, glob };
    shop.lists[gated] = { tome };
    shop.lists[outer] = { gated, open };
    shop.inventories[chest] = { outer };
    shop.vendorChests[faction] = chestRef;
    shop.refs[chestRef] = PlacedRef{ .base = chest };
    const ReachGraph shopGraph(shop);
    Sites shopSites(shop, names, shopGraph);
    Classifier classes(shop, shopGraph, shopSites, nullptr);
    const auto gateOf = [&](Key item) {
        const TomeSource s = classes.Of(item);
        return s.vendors.size() == 1 ? s.vendors[0].gate : GatedKind::kOpen;
    };
    Check(gateOf(tome) == GatedKind::kRitual, "vendor gate: a ritual-gated list below the chest's entry gates it");
    Check(gateOf(open) == GatedKind::kOpen, "vendor gate: a path around the gated list stays open");
    passed += g_passed;
    return g_failed;
}
