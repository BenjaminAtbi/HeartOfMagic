#include "tomereach/TomeReachScripts.h"
#include "tomereach/TomeReachGraph.h"
#include "AsciiText.h"

#include <algorithm>
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

    void ReachGraph::AddScripts(const DataFiles& data)
    {
        ScriptAnalysis analysis(data);
        std::unordered_set<Key> vendorRefs;
        for (const auto& [_faction, ref] : m_store.vendorChests) vendorRefs.insert(ref);
        const auto kindOf = [&](Key k) -> std::optional<Kind> {
            const auto it = m_store.kinds.find(k);
            return it != m_store.kinds.end() ? std::optional<Kind>(it->second) : std::nullopt;
        };
        const auto handedOut = [&](Key x) {
            const auto kind = kindOf(x);
            return kind == Kind::kBook || kind == Kind::kScroll || kind == Kind::kStaff || kind == Kind::kItemList;
        };
        for (const auto& [holder, h] : m_store.holders) {
            const bool noise = IsNoiseEditorId(h.editorId);
            for (const AttachedScript& script : h.scripts) {
                // Each property's forms, form lists expanded
                std::vector<std::vector<Key>> values;
                std::vector<Key> lists;
                bool stocks = false;   // the script also holds a merchant chest: its AddItem restocks it
                bool items = false;    // a property holds a tome, scroll, staff or leveled item list
                for (const ScriptProperty& p : script.properties) {
                    auto& flat = values.emplace_back();
                    for (const Key x : p.forms) {
                        flat.push_back(x);
                        if (kindOf(x) == Kind::kFormList) {
                            std::unordered_set<Key> seen;
                            FormListMembers(x, seen, flat);
                        }
                    }
                    for (const Key x : flat) {
                        const auto kind = kindOf(x);
                        if (kind == Kind::kItemList) lists.push_back(x);
                        if (vendorRefs.contains(x) || (kind == Kind::kContainer && IsVendor(x))) stocks = true;
                        items = items || handedOut(x);
                    }
                }
                if (!items) continue;   // nothing it could hand out: no need to read the script
                const ScriptGrants& grants = analysis.Of(script.name);
                for (std::size_t i = 0; i < values.size(); ++i) {
                    const bool toList = grants.toList.contains(script.properties[i].name);
                    const bool toItem = grants.toItem.contains(script.properties[i].name);
                    if (!toList && !toItem) continue;
                    for (const Key x : values[i]) {
                        if (!handedOut(x)) continue;
                        if (toList) {
                            for (const Key list : lists) {
                                if (list != x) AddEdge(x, EdgeKind::kList, list);
                            }
                        }
                        if (toItem && !stocks && !noise) m_rewards[x].push_back({ holder, false });
                    }
                }
            }
        }
        m_scriptsRead = analysis.Read();
        m_scriptsMissing = analysis.Missing();
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
