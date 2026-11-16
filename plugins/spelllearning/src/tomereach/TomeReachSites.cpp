#include "tomereach/TomeReachSites.h"
#include "tomereach/TomeReachPlugin.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <unordered_set>

namespace TomeReach
{
    namespace
    {
        constexpr std::array<std::string_view, 5> kBasePlugins = {
            "skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm",
        };
        constexpr std::string_view kTestCell = "testcell";
        constexpr std::string_view kQaPrefix = "QA";   // as written
        // A hold's location: its editor id ends so (case as written)
        constexpr std::array<std::string_view, 2> kHoldSuffixes = { "Hold", "HoldLocation" };
        constexpr std::string_view kUnknown = "?";

        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        bool IsHold(std::string_view name)
        {
            return std::any_of(kHoldSuffixes.begin(), kHoldSuffixes.end(), [&](std::string_view s) { return EndsWith(name, s); });
        }

        std::string LowerCopy(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }
    }

    std::string KeyText(const NameTable& names, Key key)
    {
        char local[16];
        std::snprintf(local, sizeof(local), "%06X", LocalOf(key));
        return names.Name(PluginOf(key)) + ":" + local;
    }

    bool IsTestCellName(std::string_view site)
    {
        if (LowerCopy(site).find(kTestCell) != std::string::npos) return true;
        if (site.size() <= kQaPrefix.size() || !site.starts_with(kQaPrefix)) return false;
        const char next = site[kQaPrefix.size()];
        return (next >= 'A' && next <= 'Z') || next == '_';
    }

    void Placements::Add(const Placements& other)
    {
        count += other.count;
        for (const auto& [where, n] : other.sites) sites[where] += n;
    }

    Sites::Sites(const SpellStore& store, const NameTable& names, const ReachGraph& graph) :
        m_store(store), m_names(names), m_graph(graph)
    {
        for (const auto& [_ref, r] : store.refs) {
            if (!(r.flags & kDeletedRecord)) m_byBase[r.base].push_back(&r);
        }
    }

    bool Sites::IsBasePlugin(Key form) const
    {
        const std::string& plugin = m_names.Name(PluginOf(form));
        return std::find(kBasePlugins.begin(), kBasePlugins.end(), plugin) != kBasePlugins.end();
    }

    std::string Sites::Realm(Key form) const
    {
        return "realm " + m_names.Name(PluginOf(form));
    }

    RegionSite Sites::Where(const PlacedRef& ref) const
    {
        static const Cell kNoCell;
        const auto c = m_store.cells.find(ref.cell);
        const Cell& cell = c != m_store.cells.end() ? c->second : kNoCell;
        const Key worldspace = cell.worldspace;
        const std::string cellName = cell.editorId.empty() ? KeyText(m_names, ref.cell) : cell.editorId;
        if (!cell.location) {
            if (worldspace && !IsBasePlugin(worldspace)) return { Realm(worldspace), cellName };
            if (!worldspace) return { "interior", cellName };
            const auto w = m_store.worldspaces.find(worldspace);
            const bool named = w != m_store.worldspaces.end() && !w->second.empty();
            return { "world " + (named ? w->second : KeyText(m_names, worldspace)), cellName };
        }
        // The location's parent chain, nearest first
        std::vector<Key> chain;
        std::vector<std::string> names;
        std::unordered_set<Key> seen;
        for (Key k = cell.location; k && !seen.contains(k);) {
            const auto l = m_store.locations.find(k);
            if (l == m_store.locations.end()) break;
            seen.insert(k);
            chain.push_back(k);
            names.push_back(l->second.editorId.empty() ? KeyText(m_names, k) : l->second.editorId);
            k = l->second.parent;
        }
        const auto hold = std::find_if(names.begin(), names.end(), [](const std::string& n) { return IsHold(n); });
        if (hold == names.end()) {
            Key top = 0;
            if (worldspace && !IsBasePlugin(worldspace)) top = worldspace;
            else if (!chain.empty()) top = chain.back();
            if (top && !IsBasePlugin(top)) {
                const std::string site = names.size() >= 2 ? names[names.size() - 2]
                                       : (names.empty() ? std::string(kUnknown) : names.front());
                return { Realm(top), site };
            }
        }
        if (names.empty()) return { std::string(kUnknown), std::string(kUnknown) };
        const std::size_t ri = hold != names.end() ? static_cast<std::size_t>(hold - names.begin()) : names.size() - 1;
        return { names[ri], ri >= 1 ? names[ri - 1] : names.front() };
    }

    const Placements& Sites::OfBase(Key base)
    {
        if (const auto it = m_ofBase.find(base); it != m_ofBase.end()) return it->second;
        Placements out;
        if (const auto refs = m_byBase.find(base); refs != m_byBase.end()) {
            for (const PlacedRef* r : refs->second) {
                RegionSite where = Where(*r);
                if (IsTestCellName(where.site)) continue;
                ++out.count;
                ++out.sites[std::move(where)];
            }
        }
        return m_ofBase.emplace(base, std::move(out)).first->second;
    }

    Placements Sites::Of(const std::vector<Key>& bases)
    {
        Placements out;
        for (const Key b : bases) out.Add(OfBase(b));
        return out;
    }

    const Placements& Sites::OfNpc(Key npc, bool inventory)
    {
        auto& memo = inventory ? m_ofNpcInventory : m_ofNpc;
        if (const auto it = memo.find(npc); it != memo.end()) return it->second;
        OfNpc(npc, inventory, {});
        return memo.at(npc);
    }

    std::string Sites::StandIn(const Spawner& spawner) const
    {
        if (spawner.kind == SpawnKind::kRef) return "REFR " + KeyText(m_names, spawner.creator);
        return spawner.label;
    }

    Placements Sites::OfNpc(Key npc, bool inventory, const std::unordered_set<Key>& outer)
    {
        auto& memo = inventory ? m_ofNpcInventory : m_ofNpc;
        if (const auto it = memo.find(npc); it != memo.end()) return it->second;
        std::unordered_set<Key> seen = outer;
        seen.insert(npc);
        const std::vector<Key> bases = m_graph.NpcBases(npc, inventory);   // sorted
        Placements out = Of(bases);
        Placements standIn;
        for (const Key b : bases) {
            for (const Spawner& spawner : m_graph.SpawnersOf(b)) {
                Placements found;
                if (spawner.kind == SpawnKind::kRef) {
                    const auto r = m_store.scriptedRefs.find(spawner.creator);
                    if (r != m_store.scriptedRefs.end()) {
                        RegionSite where = Where(r->second.placed);
                        if (!IsTestCellName(where.site)) {
                            found.count = 1;
                            ++found.sites[std::move(where)];
                        }
                    }
                } else if (spawner.kind == SpawnKind::kExplosion) {
                    for (const Key user : m_graph.ExplosionUsers(spawner.creator)) {
                        if (seen.contains(user)) continue;
                        const auto kind = m_store.kinds.find(user);
                        const bool npcLike = kind != m_store.kinds.end() &&
                                             (kind->second == Kind::kNpc || kind->second == Kind::kNpcList);
                        found.Add(npcLike ? OfNpc(user, false, seen) : OfBase(user));
                    }
                }
                if (found.count) out.Add(found);
                else {
                    ++standIn.count;
                    ++standIn.sites[{ std::string(kSpawnedRegion), StandIn(spawner) }];
                }
            }
        }
        if (!out.count && standIn.count) out = std::move(standIn);
        if (outer.empty()) memo.emplace(npc, out);   // only results a cycle did not cut short
        return out;
    }
}
