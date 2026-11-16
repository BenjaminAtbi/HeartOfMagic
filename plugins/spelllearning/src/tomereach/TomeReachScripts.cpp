#include "tomereach/TomeReachScripts.h"
#include "tomereach/TomeReachGraph.h"
#include "tomereach/TomeReachSites.h"
#include "AsciiText.h"

#include <algorithm>
#include <cstdio>
#include <optional>

namespace TomeReach
{
    namespace
    {
        constexpr std::size_t kVmadHeader = 6;           // version, object format, script count
        constexpr std::int16_t kObjectFormatV2 = 2;      // object values: unused u16, alias u16, form id
        constexpr std::size_t kObjectSize = 8;
        constexpr std::size_t kObjectFormOffsetV2 = 4;
        // Property value types; 11-15 are arrays of 1-5
        constexpr std::uint8_t kNone = 0, kObject = 1, kString = 2, kInt = 3, kFloat = 4, kBool = 5;
        constexpr std::uint8_t kArrayOffset = 10, kFirstArray = 11, kLastArray = 15;
        constexpr std::size_t kHexKeySize = 24;

        // A record with no editor id in a label: its key in hex
        std::string HexKey(Key key)
        {
            char text[kHexKeySize];
            std::snprintf(text, sizeof(text), "%08llX", static_cast<unsigned long long>(key));
            return text;
        }

        class VmadReader
        {
        public:
            VmadReader(const unsigned char* data, std::size_t size, const Resolver& resolve) :
                m_data(data), m_size(size), m_resolve(resolve)
            {}

            bool Read(std::vector<AttachedScript>& out)
            {
                if (m_size < kVmadHeader) return false;
                m_format = static_cast<std::int16_t>(U16(m_data + 2));
                const std::uint16_t count = U16(m_data + 4);
                m_at = kVmadHeader;
                for (std::uint16_t s = 0; s < count; ++s) {
                    AttachedScript script;
                    std::string name;
                    if (!WString(name)) return false;
                    script.name = AsciiText::ToLower(name);
                    ++m_at;   // status
                    if (!Has(2)) return false;
                    const std::uint16_t properties = U16(m_data + m_at);
                    m_at += 2;
                    for (std::uint16_t p = 0; p < properties; ++p) {
                        std::string pname;
                        if (!WString(pname) || !Has(1)) return false;
                        const std::uint8_t type = m_data[m_at];
                        m_at += 2;   // type, status
                        ScriptProperty prop{ AsciiText::ToLower(pname), {} };
                        if (!Value(type, prop.forms)) return false;
                        // A name given twice: the later value (Python's dict)
                        const auto same = std::find_if(script.properties.begin(), script.properties.end(),
                            [&](const ScriptProperty& x) { return x.name == prop.name; });
                        if (same != script.properties.end()) *same = std::move(prop);
                        else script.properties.push_back(std::move(prop));
                    }
                    std::erase_if(script.properties, [](const ScriptProperty& x) { return x.forms.empty(); });
                    if (!script.properties.empty()) out.push_back(std::move(script));
                }
                return true;
            }

        private:
            const unsigned char* m_data;
            std::size_t m_size;
            const Resolver& m_resolve;
            std::size_t m_at = 0;
            std::int16_t m_format = 0;

            bool Has(std::size_t n) const { return m_at <= m_size && n <= m_size - m_at; }

            bool WString(std::string& out)
            {
                if (!Has(2)) return false;
                const std::size_t n = U16(m_data + m_at);
                m_at += 2;
                const std::size_t take = m_at <= m_size ? std::min(n, m_size - m_at) : 0;
                out.assign(reinterpret_cast<const char*>(m_data + std::min(m_at, m_size)), take);
                m_at += n;
                return true;
            }

            bool Value(std::uint8_t type, std::vector<Key>& forms)
            {
                switch (type) {
                case kNone: return true;
                case kObject: {
                    if (!Has(kObjectSize)) return false;
                    const std::uint32_t id = U32(m_data + m_at + (m_format == kObjectFormatV2 ? kObjectFormOffsetV2 : 0));
                    m_at += kObjectSize;
                    if (id) forms.push_back(m_resolve(id));
                    return true;
                }
                case kString: {
                    std::string ignored;
                    return WString(ignored);
                }
                case kInt:
                case kFloat:
                    if (!Has(4)) return false;
                    m_at += 4;
                    return true;
                case kBool:
                    if (!Has(1)) return false;
                    m_at += 1;
                    return true;
                default:
                    if (type < kFirstArray || type > kLastArray || !Has(4)) return false;
                    const std::uint32_t n = U32(m_data + m_at);
                    m_at += 4;
                    if (n > m_size - m_at) return false;   // every element takes a byte at least
                    for (std::uint32_t i = 0; i < n; ++i) {
                        if (!Value(static_cast<std::uint8_t>(type - kArrayOffset), forms)) return false;
                    }
                    return true;
                }
            }
        };
    }

    std::vector<AttachedScript> ParseVmad(const unsigned char* data, std::size_t size, const Resolver& resolve)
    {
        std::vector<AttachedScript> out;
        if (!VmadReader(data, size, resolve).Read(out)) out.clear();
        return out;
    }

    const ScriptGrants& ScriptAnalysis::Of(const std::string& script)
    {
        if (const auto it = m_grants.find(script); it != m_grants.end()) return it->second;
        std::vector<unsigned char> data;
        ScriptGrants grants;
        if (m_data.Read("scripts/" + script + ".pex", data)) {
            ++m_read;
            grants = AnalyseScript(data);
        } else {
            ++m_missing;
        }
        return m_grants.emplace(script, std::move(grants)).first->second;
    }

    std::vector<ReachGraph::ScriptSource> ReachGraph::ScriptSources()
    {
        std::vector<ScriptSource> out;
        for (const auto& [holder, h] : m_store.holders) {
            const std::string type(h.type.begin(), h.type.end());
            out.push_back({ holder, {}, type + " " + (h.editorId.empty() ? HexKey(holder) : h.editorId),
                IsNoiseEditorId(h.editorId), &h.scripts });
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.holder < b.holder; });
        // Placed references: the winning version's scripts, deleted ones and developer test cells left out; those
        // sharing a base and a cell share a label, and the first one's scripts stand for them all
        std::vector<Key> refs;
        for (const auto& [ref, _r] : m_store.scriptedRefs) refs.push_back(ref);
        std::sort(refs.begin(), refs.end());
        std::unordered_map<std::string, std::size_t> byLabel;
        for (const Key ref : refs) {
            const ScriptedRef& r = m_store.scriptedRefs.at(ref);
            if (r.placed.flags & kDeletedRecord) continue;
            const auto c = m_store.cells.find(r.cell);
            const std::string cell = c != m_store.cells.end() ? c->second.editorId : std::string();
            if (IsTestCellName(cell)) continue;
            const std::string& baseId = EditorId(r.placed.base);
            const std::string base = baseId.empty() ? HexKey(r.placed.base) : baseId;
            const std::string label = "REFR " + base + " in " + (cell.empty() ? HexKey(r.cell) : cell);
            const auto [at, added] = byLabel.try_emplace(label, out.size());
            if (added) out.push_back({ ref, {}, label, IsNoiseEditorId(base), &r.scripts });
            out[at->second].refs.push_back(ref);
        }
        for (const ScriptSource& source : out) m_holderLabels[source.holder] = source.label;
        return out;
    }

    void ReachGraph::AddScripts(const DataFiles& data)
    {
        ScriptAnalysis analysis(data);
        const std::vector<ScriptSource> sources = ScriptSources();
        std::unordered_set<Key> vendorRefs;
        for (const auto& [_faction, ref] : m_store.vendorChests) vendorRefs.insert(ref);
        const auto handedOut = [&](Key x) {
            return KindIs(x, Kind::kBook) || KindIs(x, Kind::kScroll) || KindIs(x, Kind::kStaff) || KindIs(x, Kind::kItemList);
        };
        const auto spawnable = [&](Key x) { return KindIs(x, Kind::kNpc) || KindIs(x, Kind::kNpcList); };
        const auto property = [](const AttachedScript& script, const std::string& name) -> const ScriptProperty* {
            const auto it = std::find_if(script.properties.begin(), script.properties.end(),
                [&](const ScriptProperty& p) { return p.name == name; });
            return it != script.properties.end() ? &*it : nullptr;
        };
        const auto flat = [&](const std::vector<Key>& forms, bool runtime) {
            std::vector<Key> out;
            Flat(forms, runtime, out);
            return out;
        };
        // A script is read only when a property could matter: an item, a list, a form list or an NPC
        static const ScriptGrants kNothing;
        const auto grantsOf = [&](const AttachedScript& script) -> const ScriptGrants& {
            for (const ScriptProperty& p : script.properties) {
                const auto forms = flat(p.forms, false);
                if (std::any_of(forms.begin(), forms.end(),
                        [&](Key x) { return handedOut(x) || spawnable(x) || KindIs(x, Kind::kFormList); }))
                    return analysis.Of(script.name);
            }
            return kNothing;
        };
        // `List.AddForm(Item)`: a leveled list gets a parent, a form list a run-time member. Every pair first: a
        // reward through a form list reads what scripts add to it
        for (const ScriptSource& source : sources) {
            for (const AttachedScript& script : *source.scripts) {
                for (const AddFormPair& pair : grantsOf(script).pairs) {
                    const ScriptProperty* receiver = property(script, pair.receiver);
                    const ScriptProperty* argument = property(script, pair.argument);
                    if (!receiver || !argument) continue;
                    auto items = flat(argument->forms, false);
                    std::erase_if(items, [&](Key x) { return !handedOut(x); });
                    // The receiver itself, or (an element) any member of the form list / array it comes from
                    const auto receivers = pair.receiverElement ? flat(receiver->forms, false) : receiver->forms;
                    for (const Key list : receivers) {
                        if (KindIs(list, Kind::kItemList)) {
                            for (const Key x : items) {
                                if (x != list) AddEdge(x, EdgeKind::kList, list);
                            }
                        } else if (KindIs(list, Kind::kFormList)) {
                            auto& members = m_runtimeLists[list];
                            for (const Key x : items) {
                                if (std::find(members.begin(), members.end(), x) == members.end()) members.push_back(x);
                            }
                        }
                    }
                }
            }
        }
        // AddItem / PlaceAtMe: a reward, unless the script also holds a merchant chest (it restocks it).
        // PlaceAtMe / PlaceActorAtMe of an NPC: a spawner
        std::vector<std::vector<Key>> spawned(sources.size());
        for (std::size_t s = 0; s < sources.size(); ++s) {
            const ScriptSource& source = sources[s];
            if (source.noise) continue;
            for (const AttachedScript& script : *source.scripts) {
                const ScriptGrants& grants = grantsOf(script);
                for (const std::string& name : grants.spawn) {
                    if (const ScriptProperty* p = property(script, name)) {
                        for (const Key x : flat(p->forms, false)) {
                            if (spawnable(x)) spawned[s].push_back(x);
                        }
                    }
                }
                if (grants.give.empty()) continue;
                const bool stocks = std::any_of(script.properties.begin(), script.properties.end(), [&](const ScriptProperty& p) {
                    const auto forms = flat(p.forms, false);
                    return std::any_of(forms.begin(), forms.end(),
                        [&](Key x) { return vendorRefs.contains(x) || (KindIs(x, Kind::kContainer) && IsVendor(x)); });
                });
                if (stocks) continue;
                for (const std::string& name : grants.give) {
                    if (const ScriptProperty* p = property(script, name)) {
                        for (const Key x : flat(p->forms, true)) {
                            if (handedOut(x)) m_rewards[x].push_back({ source.holder, false });
                        }
                    }
                }
            }
        }
        AddSpawners(sources, spawned);
        m_scriptsRead = analysis.Read();
        m_scriptsMissing = analysis.Missing();
    }

    void ReachGraph::AddSpawners(const std::vector<ScriptSource>& sources, const std::vector<std::vector<Key>>& spawned)
    {
        // On a placed reference the NPC appears where the reference is; on a record, wherever that record is
        for (std::size_t s = 0; s < sources.size(); ++s) {
            for (const Key npc : spawned[s]) {
                auto& spawners = m_spawners[npc];
                if (sources[s].refs.empty()) spawners.push_back({ SpawnKind::kScript, sources[s].holder, sources[s].label });
                for (const Key ref : sources[s].refs) spawners.push_back({ SpawnKind::kRef, ref, "REFR " + HexKey(ref) });
            }
        }
    }

    void ReachGraph::Flat(const std::vector<Key>& forms, bool runtime, std::vector<Key>& out) const
    {
        for (const Key x : forms) {
            out.push_back(x);
            if (!KindIs(x, Kind::kFormList)) continue;
            std::unordered_set<Key> seen;
            FormListMembers(x, seen, out);
            if (!runtime) continue;
            if (const auto it = m_runtimeLists.find(x); it != m_runtimeLists.end())
                out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }

    void ReachGraph::FormListMembers(Key list, std::unordered_set<Key>& seen, std::vector<Key>& out) const
    {
        const auto it = m_store.formLists.find(list);
        if (it == m_store.formLists.end() || !seen.insert(list).second) return;
        for (const Key x : it->second) {
            const auto kind = m_store.kinds.find(x);
            if (kind != m_store.kinds.end() && kind->second == Kind::kFormList) FormListMembers(x, seen, out);
            else out.push_back(x);
        }
    }
}
