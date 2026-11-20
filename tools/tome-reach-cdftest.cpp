// =============================================================================
// tome-reach-test --self-test  -  Container Distribution Framework rules
// =============================================================================
//
// Checks TomeReachCdf on made-up CDF files (what CDF reads, rejects and stops at)
// and the reach graph's container edges on a hand-built store (adds, replaces,
// counted removes, vendor chests, rules naming no container). Called from
// tome-reach-selftest.cpp.

#include "tomereach/TomeReachCdf.h"
#include "tomereach/TomeReachGraph.h"

#include <algorithm>
#include <cstdio>

namespace
{
    using namespace TomeReach;

    int g_passed = 0;
    int g_failed = 0;

    void Check(bool ok, const char* what)
    {
        (ok ? g_passed : g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }

    bool Has(const std::vector<Key>& v, Key x) { return std::find(v.begin(), v.end(), x) != v.end(); }

    bool AnyContains(const std::vector<std::string>& v, std::string_view text)
    {
        return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return s.find(text) != std::string::npos; });
    }

    const std::unordered_set<std::string> kActive = { "model.esm" };

    void ReaderChecks()
    {
        NameTable names;
        std::vector<RawCdfChange> out;
        std::vector<std::string> warn;
        ReadCdf("\xEF\xBB\xBF{\"rules\": [{\"friendlyName\": \"A\", \"conditions\": {\"containers\": [\"Model.esm|0x860\"],"
                "\"allowVendors\": true}, \"changes\": [{\"add\": [\"0x800~Model.esm\"]}, {\"remove\": \"Model.esm|0x801\","
                "\"count\": 1}, {\"count\": 2}]}]}",
            "a.json", kActive, names, out, warn);
        Check(out.size() == 2 && warn.empty() && out[0].vendors && out[0].containers.size() == 1 && out[0].addEntries == 1 &&
                  out[1].hasRemove && out[1].countGiven,
            "CDF: a BOM is skipped; add and remove changes read, one changing nothing left out");

        out.clear();
        warn.clear();
        ReadCdf("{\"rules\": [\n{\"friendlyName\": \"A\", \"changes\": [{\"add\": [\"X\"]}]},\n"
                "{\"friendlyName\": \"B\", \"changes\": [{\"add\": [\"Y\"]}\n",
            "cut.json", kActive, names, out, warn);
        Check(out.size() == 1 && out[0].rule == "A" && AnyContains(warn, "syntax error at line") &&
                  AnyContains(warn, "only the 1 rules"),
            "CDF: a syntax error keeps the whole rules before it, not the rule it cuts");

        out.clear();
        warn.clear();
        ReadCdf("{\"rules\": [{\"friendlyName\": \"A\", \"changes\": [{\"add\": [\"X\"]}]}, {\"changes\": []},"
                "{\"friendlyName\": \"C\", \"changes\": [{\"add\": [\"Z\"]}]}]}",
            "noname.json", kActive, names, out, warn);
        Check(out.size() == 1 && AnyContains(warn, "lacks friendlyName"), "CDF: a rule without friendlyName ends its file");

        out.clear();
        warn.clear();
        ReadCdf("{\"rules\": [{\"friendlyName\": \"A\", \"conditions\": {\"plugins\": [\"Missing.esp\"]}, \"changes\": "
                "[{\"add\": [\"X\"]}]}, {\"friendlyName\": \"B\", \"changes\": [{\"add\": [\"Y\"]}]}]}",
            "plugins.json", kActive, names, out, warn);
        Check(out.empty() && AnyContains(warn, "needs Missing.esp"), "CDF: a plugins condition naming an inactive plugin ends its file");

        out.clear();
        warn.clear();
        ReadCdf("{\"rules\": [{\"friendlyName\": \"A\", \"changes\": [{\"remove\": [\"X\"]}, {\"add\": \"Y\"}, "
                "{\"add\": [\"Z\", 5]}]}]}",
            "bad.json", kActive, names, out, warn);
        Check(out.size() == 1 && out[0].addEntries == 2 && out[0].add.size() == 1 && AnyContains(warn, "not a single string"),
            "CDF: a remove array or an add that is no list drops the change; non-string add entries count");
        SpellStore store;
        const auto resolved = ResolveCdf(out, store, warn);
        Check(resolved.empty() && AnyContains(warn, "an add entry doesn't resolve"),
            "CDF: an add entry that does not resolve drops the change");
    }

    void GraphChecks()
    {
        NameTable names;
        const std::uint32_t plugin = names.Id("Model.esm");
        const auto k = [&](std::uint32_t id) { return MakeKey(plugin, id); };
        const Key tome = k(0x800), old = k(0x801), kept = k(0x802);
        const Key chest = k(0x860), other = k(0x861), vendor = k(0x862), faction = k(0x863), vendorRef = k(0x870);
        SpellStore store;
        for (const Key b : { tome, old, kept }) store.kinds[b] = Kind::kBook;
        for (const Key c : { chest, other, vendor }) store.kinds[c] = Kind::kContainer;
        store.kinds[faction] = Kind::kFaction;
        store.inventories[chest] = { old };
        store.inventories[other] = { kept };
        store.vendorChests[faction] = vendorRef;
        store.refs[vendorRef] = PlacedRef{ .base = vendor };

        Distribution d;
        d.cdf.push_back({ .rule = "replace", .containers = { chest, vendor }, .op = CdfOp::kReplace, .add = { tome }, .remove = { old } });
        d.cdf.push_back({ .rule = "counted", .containers = { other }, .op = CdfOp::kRemove, .remove = { kept }, .countGiven = true });
        d.cdf.push_back({ .rule = "anywhere", .op = CdfOp::kAdd, .add = { kept } });
        const ReachGraph graph(store, &d);
        const auto holders = graph.HoldersOf(tome);
        Check(Has(holders, chest) && !Has(graph.HoldersOf(old), chest), "CDF graph: a replace adds the new form and takes the old out");
        Check(!Has(holders, vendor), "CDF graph: a vendor chest takes a rule only with allowVendors");
        Check(Has(graph.HoldersOf(kept), other), "CDF graph: a counted remove leaves the form in the container");
        Check(graph.CdfUntraced() == 1 && !Has(graph.HoldersOf(kept), chest), "CDF graph: a rule naming no container is not traced");

        d.cdf = { { .rule = "vendors", .containers = { vendor }, .vendors = true, .op = CdfOp::kAdd, .add = { tome } } };
        Check(Has(ReachGraph(store, &d).HoldersOf(tome), vendor), "CDF graph: allowVendors reaches a vendor chest");
    }
}

int RunCdfChecks(int& passed)
{
    ReaderChecks();
    GraphChecks();
    passed += g_passed;
    return g_failed;
}
