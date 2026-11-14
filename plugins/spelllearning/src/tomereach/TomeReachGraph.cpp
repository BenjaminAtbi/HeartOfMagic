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
        // Editor id patterns (scripts/spellmap/reach.py NOISE and VENDOR), case ignored
        constexpr std::array<std::string_view, 6> kNoisePrefixes = { "qa", "dbm_", "eli_awesome", "zzztest", "test", "aaa" };
        constexpr std::array<std::string_view, 3> kNoiseWords = { "debug", "cheat", "dummy" };
        constexpr std::array<std::string_view, 4> kVendorWords = { "vendor", "merch", "shop", "store" };

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
        const std::string kNoName;

        char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

        bool StartsWithNoCase(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() &&
                   std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) { return a == Lower(b); });
        }

        bool ContainsNoCase(std::string_view text, std::string_view word)
        {
            return std::search(text.begin(), text.end(), word.begin(), word.end(),
                       [](char a, char b) { return Lower(a) == b; }) != text.end();
        }

        bool ContainsAny(std::string_view text, auto const& words)
        {
            return std::any_of(words.begin(), words.end(), [&](std::string_view w) { return ContainsNoCase(text, w); });
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
        return std::any_of(kNoisePrefixes.begin(), kNoisePrefixes.end(),
                   [&](std::string_view p) { return StartsWithNoCase(editorId, p); }) ||
               ContainsAny(editorId, kNoiseWords);
    }

    ReachGraph::ReachGraph(const SpellStore& store) :
        m_store(store)
    {
        for (const auto& [list, entries] : store.lists) {
            const bool items = store.kinds.at(list) == Kind::kItemList;
            for (const Key e : entries) {
                if (items) AddEdge(e, EdgeKind::kList, list);
                else m_npcParents[e].push_back(list);
            }
        }
        for (const auto& [holder, items] : store.inventories) {
            if (store.kinds.at(holder) != Kind::kContainer || IsNoiseEditorId(EditorId(holder))) continue;
            for (const Key e : items) AddEdge(e, EdgeKind::kContainer, holder);
        }
        for (const auto& [npc, data] : store.npcs) {
            if (data.templ) m_npcParents[data.templ].push_back(npc);
            if (IsNoiseEditorId(EditorId(npc))) continue;
            for (const Key e : data.items) AddEdge(e, EdgeKind::kNpc, npc);
            if (data.deathItem) AddEdge(data.deathItem, EdgeKind::kNpc, npc);
        }
        for (auto& [_child, parents] : m_npcParents) SortUnique(parents);
        for (const auto& [quest, items] : store.questItems) {
            if (IsNoiseEditorId(EditorId(quest))) continue;
            for (const Key e : items) m_rewards[e].push_back(quest);
        }
        for (auto& [_item, quests] : m_rewards) SortUnique(quests);
        for (const auto& [recipe, r] : store.recipes) m_recipes[r.created].push_back(recipe);
        for (auto& [_item, recipes] : m_recipes) std::sort(recipes.begin(), recipes.end());
        for (const auto& [_faction, ref] : store.vendorChests) {
            const auto r = store.refs.find(ref);
            if (r == store.refs.end()) continue;
            const auto kind = store.kinds.find(r->second.base);
            if (kind != store.kinds.end() && kind->second == Kind::kContainer) m_vendorBases.insert(r->second.base);
        }
    }

    void ReachGraph::AddEdge(Key child, EdgeKind kind, Key parent)
    {
        auto& edges = m_parents[child];
        const Edge edge{ kind, parent };
        if (std::find(edges.begin(), edges.end(), edge) == edges.end()) edges.push_back(edge);
    }

    const std::string& ReachGraph::EditorId(Key form) const
    {
        const auto it = m_store.editorIds.find(form);
        return it != m_store.editorIds.end() ? it->second : kNoName;
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
                    out.roots.push_back({ e.kind == EdgeKind::kContainer ? RootKind::kContainer : RootKind::kNpc, e.parent });
                }
            }
        }
        out.lists.assign(seen.begin(), seen.end());
        SortUnique(out.roots);
        SortUnique(out.lists);
        return out;
    }

    std::vector<Key> ReachGraph::NpcBases(Key npc) const
    {
        std::unordered_set<Key> out{ npc };
        std::vector<Key> todo{ npc };
        while (!todo.empty()) {
            const Key x = todo.back();
            todo.pop_back();
            const auto p = m_npcParents.find(x);
            if (p == m_npcParents.end()) continue;
            for (const Key parent : p->second) {
                if (out.insert(parent).second) todo.push_back(parent);
            }
        }
        std::vector<Key> bases(out.begin(), out.end());
        std::sort(bases.begin(), bases.end());
        return bases;
    }

    bool ReachGraph::IsVendor(Key container) const
    {
        return m_vendorBases.contains(container) || ContainsAny(EditorId(container), kVendorWords);
    }

    const std::vector<Key>& ReachGraph::RewardsOf(Key item) const
    {
        const auto it = m_rewards.find(item);
        return it != m_rewards.end() ? it->second : kNone;
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
