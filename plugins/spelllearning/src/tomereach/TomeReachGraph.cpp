#include "tomereach/TomeReachGraph.h"
#include "tomereach/TomeReachPlugin.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace TomeReach
{
    namespace
    {
        // Editor id patterns (scripts/spellmap/reach.py NOISE and VENDOR), case ignored except QA's
        constexpr std::array<std::string_view, 5> kNoisePrefixes = { "dbm_", "eli_awesome", "zzztest", "test", "aaa" };
        constexpr std::array<std::string_view, 3> kNoiseWords = { "debug", "cheat", "dummy" };
        // QA, as written, and not followed by a lower case letter: Vominheim's QAlderim is a real quest
        constexpr std::string_view kQaPrefix = "QA";
        constexpr std::array<std::string_view, 3> kVendorWords = { "vendor", "shop", "store" };
        // "merch" (merchant, merchandise), but not "merchest": Treas*FalmerChest*, a Falmer loot chest
        constexpr std::string_view kMerch = "merch";
        constexpr std::string_view kNotMerch = "est";

        // ACBS template flags: Use Inventory supplies the inventory and the default outfit, Use Traits the death item
        constexpr std::uint16_t kUseTraits = 0x01;
        constexpr std::uint16_t kUseInventory = 0x100;

        // CTDA: type byte, 3 pad, comparison value (float), function (u16), 2 pad, parameter 1, parameter 2, run on
        constexpr std::size_t kConditionValueOffset = 4;
        constexpr std::size_t kConditionFunctionOffset = 8;
        constexpr std::size_t kConditionParam1Offset = 12;
        constexpr std::size_t kConditionParam2Offset = 16;
        constexpr std::size_t kConditionRunOnOffset = 20;
        constexpr std::size_t kConditionMinSize = 20;
        constexpr std::uint8_t kConditionUsesGlobal = 0x04;
        constexpr unsigned kConditionOperatorShift = 5;   // 0: ==
        constexpr std::uint16_t kGetIsId = 72;
        constexpr std::uint32_t kPlayerBase = 0x7;

        const std::vector<Key> kNone;
        const std::vector<Reward> kNoRewards;
        const std::vector<Spawner> kNoSpawners;
        const std::string kNoName;

        char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

        bool StartsWithNoCase(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() &&
                   std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) { return a == Lower(b); });
        }

        std::size_t FindNoCase(std::string_view text, std::string_view word, std::size_t from = 0)
        {
            if (from > text.size()) return std::string_view::npos;
            const auto it = std::search(text.begin() + static_cast<std::ptrdiff_t>(from), text.end(), word.begin(),
                word.end(), [](char a, char b) { return Lower(a) == b; });
            return it == text.end() ? std::string_view::npos : static_cast<std::size_t>(it - text.begin());
        }

        bool ContainsAny(std::string_view text, auto const& words)
        {
            return std::any_of(words.begin(), words.end(),
                [&](std::string_view w) { return FindNoCase(text, w) != std::string_view::npos; });
        }

        bool IsVendorName(std::string_view editorId)
        {
            if (ContainsAny(editorId, kVendorWords)) return true;
            for (auto at = FindNoCase(editorId, kMerch); at != std::string_view::npos; at = FindNoCase(editorId, kMerch, at + 1)) {
                if (!StartsWithNoCase(editorId.substr(at + kMerch.size()), kNotMerch)) return true;
            }
            return false;
        }

        template <class T>
        void SortUnique(std::vector<T>& v)
        {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        }

        float F32(const unsigned char* p)
        {
            float f;
            std::memcpy(&f, p, sizeof(f));
            return f;
        }
    }

    bool IsNoiseEditorId(std::string_view editorId)
    {
        const bool qa = editorId.starts_with(kQaPrefix) &&
                        !(editorId.size() > kQaPrefix.size() && editorId[kQaPrefix.size()] >= 'a' &&
                            editorId[kQaPrefix.size()] <= 'z');
        return qa ||
               std::any_of(kNoisePrefixes.begin(), kNoisePrefixes.end(),
                   [&](std::string_view p) { return StartsWithNoCase(editorId, p); }) ||
               ContainsAny(editorId, kNoiseWords);
    }

    ReachGraph::ReachGraph(const SpellStore& store, const Distribution* distribution, const DataFiles* data) :
        m_store(store)
    {
        for (const auto& [list, entries] : store.lists) {
            const bool items = store.kinds.at(list) == Kind::kItemList;
            for (const Key e : entries) {
                if (items) AddEdge(e, EdgeKind::kList, list);
                else m_npcParents[e].push_back(list);
            }
        }
        for (const auto& [_faction, ref] : store.vendorChests) {
            const auto r = store.refs.find(ref);
            if (r == store.refs.end()) continue;
            if (KindIs(r->second.base, Kind::kContainer)) m_vendorBases.insert(r->second.base);
        }
        // Vendors by name: only containers placed somewhere (an unplaced one stocks nobody)
        for (const auto& [_ref, r] : store.refs) {
            if (!(r.flags & kDeletedRecord) && KindIs(r.base, Kind::kContainer) && !m_vendorBases.contains(r.base) &&
                IsVendorName(EditorId(r.base)))
                m_namedVendors.insert(r.base);
        }
        if (distribution) {
            // SkyPatcher add(Once)ToLLs: a list parent at run time; removeFromLLs: no longer in that list
            for (const ListAdd& add : distribution->listAdds) {
                if (add.remove) continue;
                for (const Key list : add.lists) AddEdge(add.form, EdgeKind::kList, list);
            }
            for (const ListAdd& add : distribution->listAdds) {
                if (!add.remove) continue;
                for (const Key list : add.lists) RemoveEdge(add.form, EdgeKind::kList, list);
            }
        }
        for (const auto& [holder, items] : store.inventories) {
            if (store.kinds.at(holder) != Kind::kContainer || store.noisy.contains(holder)) continue;
            for (const Key e : items) AddEdge(e, EdgeKind::kContainer, holder);
        }
        if (distribution) {
            // CDF (reach.py _cdf): an add is an entry of each named container, a replace or an uncounted remove
            // takes the removed form out of it; a vendor chest only with allowVendors. Rules naming no container
            // and keyword removals are not traced
            for (const CdfChange& c : distribution->cdf) {
                if (c.containers.empty()) {
                    ++m_cdfUntraced;
                    continue;
                }
                const bool removes = c.op == CdfOp::kReplace || (c.op == CdfOp::kRemove && !c.countGiven);
                for (const Key k : c.containers) {
                    if (!KindIs(k, Kind::kContainer) || (IsVendor(k) && !c.vendors)) continue;
                    if (removes) {
                        for (const Key r : c.remove) RemoveEdge(r, EdgeKind::kContainer, k);
                    }
                    for (const Key e : c.add) AddEdge(e, EdgeKind::kContainer, k);
                }
            }
        }
        for (const auto& [npc, record] : store.npcs) {
            if (record.templ) m_npcParents[record.templ].push_back(npc);
        }
        for (auto& [_child, parents] : m_npcParents) SortUnique(parents);
        // Each NPC's effective inventory, template flags resolved
        for (const auto& [npc, _record] : store.npcs) {
            if (store.noisy.contains(npc)) continue;
            for (const NpcPart part : { NpcPart::kInventory, NpcPart::kDeathItem, NpcPart::kOutfit }) {
                for (const Key e : NpcItems(npc, part)) AddEdge(e, EdgeKind::kNpc, npc);
            }
        }
        for (const auto& [quest, items] : store.questItems) {
            if (store.noisy.contains(quest)) continue;
            for (const Key e : items) m_rewards[e].push_back({ quest, true });
        }
        for (const auto& [recipe, r] : store.recipes) m_recipes[r.created].push_back(recipe);
        for (auto& [_item, recipes] : m_recipes) std::sort(recipes.begin(), recipes.end());
        // SPID lines: roots of their own
        if (distribution) {
            for (std::size_t i = 0; i < distribution->spid.size(); ++i) AddEdge(distribution->spid[i].form, EdgeKind::kSpid, i);
        }
        // Spawned NPCs: quest aliases that create one, explosions that place one (scripts: AddScripts)
        const auto spawnable = [&](Key k) { return KindIs(k, Kind::kNpc) || KindIs(k, Kind::kNpcList); };
        for (const auto& [quest, created] : store.questCreates) {
            if (store.noisy.contains(quest)) continue;
            for (const Key e : created) {
                if (spawnable(e)) m_spawners[e].push_back({ SpawnKind::kAlias, quest, "QUST " + EditorId(quest) + " (alias)" });
            }
        }
        for (const auto& [explosion, x] : store.explosions) {
            if (spawnable(x.placed)) m_spawners[x.placed].push_back({ SpawnKind::kExplosion, explosion, "EXPL " + x.editorId });
        }
        for (const auto& [record, explosions] : store.destruction) {
            for (const Key e : explosions) m_explosionUsers[e].push_back(record);
        }
        for (auto& [_e, users] : m_explosionUsers) SortUnique(users);
        if (data) AddScripts(*data);
        for (auto& [_item, rewards] : m_rewards) SortUnique(rewards);
        for (auto& [_npc, spawners] : m_spawners) SortUnique(spawners);
    }

    void ReachGraph::AddEdge(Key child, EdgeKind kind, Key parent)
    {
        auto& edges = m_parents[child];
        const Edge edge{ kind, parent };
        if (std::find(edges.begin(), edges.end(), edge) == edges.end()) edges.push_back(edge);
    }

    void ReachGraph::RemoveEdge(Key child, EdgeKind kind, Key parent)
    {
        const auto it = m_parents.find(child);
        if (it != m_parents.end()) std::erase(it->second, Edge{ kind, parent });
    }

    const std::unordered_set<Key>& ReachGraph::NpcItems(Key npc, NpcPart part) const
    {
        auto& memo = m_npcItems[static_cast<std::size_t>(part)];
        if (const auto it = memo.find(npc); it != memo.end()) return it->second;
        auto items = CollectNpcItems(npc, part, {});
        return memo.emplace(npc, std::move(items)).first->second;
    }

    std::unordered_set<Key> ReachGraph::CollectNpcItems(Key npc, NpcPart part, std::unordered_set<Key> seen) const
    {
        const auto& memo = m_npcItems[static_cast<std::size_t>(part)];
        if (const auto it = memo.find(npc); it != memo.end()) return it->second;
        std::unordered_set<Key> out;
        const auto n = m_store.npcs.find(npc);
        if (n == m_store.npcs.end()) return out;
        const Npc& record = n->second;
        const std::uint16_t flag = part == NpcPart::kDeathItem ? kUseTraits : kUseInventory;
        if (record.templ && (record.templateFlags & flag)) {
            seen.insert(npc);
            std::vector<Key> todo{ record.templ };
            std::unordered_set<Key> done;
            while (!todo.empty()) {
                const Key x = todo.back();
                todo.pop_back();
                if (seen.contains(x) || !done.insert(x).second) continue;
                if (KindIs(x, Kind::kNpcList)) {
                    if (const auto l = m_store.lists.find(x); l != m_store.lists.end()) todo.insert(todo.end(), l->second.begin(), l->second.end());
                } else if (KindIs(x, Kind::kNpc)) {
                    const auto more = CollectNpcItems(x, part, seen);
                    out.insert(more.begin(), more.end());
                }
            }
        } else if (part == NpcPart::kInventory) {
            out.insert(record.items.begin(), record.items.end());
        } else if (part == NpcPart::kOutfit) {
            if (record.outfit && KindIs(record.outfit, Kind::kOutfit)) {
                if (const auto o = m_store.inventories.find(record.outfit); o != m_store.inventories.end())
                    out.insert(o->second.begin(), o->second.end());
            }
        } else if (record.deathItem) {
            out.insert(record.deathItem);
        }
        return out;
    }

    const std::string& ReachGraph::EditorId(Key form) const
    {
        const auto it = m_store.editorIds.find(form);
        return it != m_store.editorIds.end() ? it->second : kNoName;
    }

    bool ReachGraph::KindIs(Key form, Kind kind) const
    {
        const auto it = m_store.kinds.find(form);
        return it != m_store.kinds.end() && it->second == kind;
    }

    Roots ReachGraph::RootsOf(Key item) const
    {
        Roots out;
        std::unordered_set<Key> seen{ item };
        std::vector<Key> todo{ item };
        while (!todo.empty()) {
            const Key x = todo.back();
            todo.pop_back();
            const auto p = m_parents.find(x);
            if (p == m_parents.end()) continue;
            for (const Edge& e : p->second) {
                if (e.kind == EdgeKind::kList) {
                    if (seen.insert(e.parent).second) todo.push_back(e.parent);
                } else {
                    const RootKind kind = e.kind == EdgeKind::kContainer ? RootKind::kContainer
                                        : e.kind == EdgeKind::kNpc       ? RootKind::kNpc
                                                                         : RootKind::kSpid;
                    out.roots.push_back({ kind, e.parent });
                }
            }
        }
        out.lists.assign(seen.begin(), seen.end());
        SortUnique(out.roots);
        SortUnique(out.lists);
        return out;
    }

    std::vector<Key> ReachGraph::HoldersOf(Key item) const
    {
        std::vector<Key> out;
        if (const auto p = m_parents.find(item); p != m_parents.end()) {
            for (const Edge& e : p->second) {
                if (e.kind == EdgeKind::kContainer) out.push_back(e.parent);
            }
        }
        return out;
    }

    std::vector<Key> ReachGraph::ListsHolding(Key item) const
    {
        std::vector<Key> out;
        if (const auto p = m_parents.find(item); p != m_parents.end()) {
            for (const Edge& e : p->second) {
                if (e.kind == EdgeKind::kList) out.push_back(e.parent);
            }
        }
        return out;
    }

    std::vector<Key> ReachGraph::NpcBases(Key npc, bool inventory) const
    {
        std::unordered_set<Key> out{ npc };
        std::vector<Key> todo{ npc };
        while (!todo.empty()) {
            const Key x = todo.back();
            todo.pop_back();
            const auto p = m_npcParents.find(x);
            if (p == m_npcParents.end()) continue;
            for (const Key parent : p->second) {
                if (inventory && KindIs(parent, Kind::kNpc)) continue;   // template children: their own inventory
                if (out.insert(parent).second) todo.push_back(parent);
            }
        }
        std::vector<Key> bases(out.begin(), out.end());
        std::sort(bases.begin(), bases.end());
        return bases;
    }

    bool ReachGraph::IsVendor(Key container) const
    {
        return m_vendorBases.contains(container) || m_namedVendors.contains(container);
    }

    const std::vector<Reward>& ReachGraph::RewardsOf(Key item) const
    {
        const auto it = m_rewards.find(item);
        return it != m_rewards.end() ? it->second : kNoRewards;
    }

    const std::vector<Spawner>& ReachGraph::SpawnersOf(Key npc) const
    {
        const auto it = m_spawners.find(npc);
        return it != m_spawners.end() ? it->second : kNoSpawners;
    }

    const std::vector<Key>& ReachGraph::ExplosionUsers(Key explosion) const
    {
        const auto it = m_explosionUsers.find(explosion);
        return it != m_explosionUsers.end() ? it->second : kNone;
    }

    std::string ReachGraph::HolderLabel(Key holder) const
    {
        if (const auto it = m_holderLabels.find(holder); it != m_holderLabels.end()) return it->second;
        return EditorId(holder);
    }

    const std::vector<Key>& ReachGraph::RecipesOf(Key item) const
    {
        const auto it = m_recipes.find(item);
        return it != m_recipes.end() ? it->second : kNone;
    }

    bool ReachGraph::RecipeDisabled(Key recipe) const
    {
        const auto it = m_store.recipes.find(recipe);
        if (it == m_store.recipes.end()) return false;
        // As the Python model reads it: subject, GetIsID with parameter 7 (the player) and no second
        // parameter, == a value from 0 up to (not including) 1
        return std::any_of(it->second.conditions.begin(), it->second.conditions.end(), [](const auto& c) {
            if (c.size() < kConditionMinSize) return false;
            const unsigned char* p = c.data();
            const std::uint32_t runOn = c.size() >= kConditionRunOnOffset + 4 ? U32(p + kConditionRunOnOffset) : 0;
            const float value = F32(p + kConditionValueOffset);
            return (p[0] >> kConditionOperatorShift) == 0 && !(p[0] & kConditionUsesGlobal) &&
                   U16(p + kConditionFunctionOffset) == kGetIsId && U32(p + kConditionParam1Offset) == kPlayerBase &&
                   U32(p + kConditionParam2Offset) == 0 && runOn == 0 && value >= 0.0f && value < 1.0f;
        });
    }
}
