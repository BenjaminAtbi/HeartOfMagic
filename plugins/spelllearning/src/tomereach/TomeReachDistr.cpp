#include "tomereach/TomeReachDistr.h"
#include "AsciiText.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <cstring>
#include <unordered_set>

namespace TomeReach
{
    namespace
    {
        constexpr std::string_view kSpace = " \t\r\n\v\f\x1c\x1d\x1e\x1f";
        constexpr std::uint32_t kLocalBits = 0xFFFFFF;
        constexpr std::size_t kLocalHexDigits = 6;
        constexpr std::size_t kSpidFields = 7;          // forms|strings|forms filter|level|traits|count|chance
        constexpr std::uint32_t kPlayerLocal = 0x7;     // skyrim.esm: the player, left out of the NPCs
        constexpr std::string_view kNone = "none";
        // String table: count, data size, then (id, offset) pairs; offsets from the end of the directory
        constexpr std::size_t kStringsHeader = 8;
        constexpr std::size_t kStringsEntry = 8;

        std::string_view Trim(std::string_view s)
        {
            const auto b = s.find_first_not_of(kSpace);
            if (b == std::string_view::npos) return {};
            return s.substr(b, s.find_last_not_of(kSpace) - b + 1);
        }

        std::vector<std::string_view> Split(std::string_view s, char sep)
        {
            std::vector<std::string_view> out;
            std::size_t at = 0;
            while (true) {
                const auto next = s.find(sep, at);
                out.push_back(s.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at));
                if (next == std::string_view::npos) return out;
                at = next + 1;
            }
        }

        std::vector<std::string_view> Lines(std::string_view text)
        {
            std::vector<std::string_view> out;
            std::size_t at = 0;
            while (at < text.size()) {
                const auto end = text.find_first_of("\r\n", at);
                out.push_back(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
                if (end == std::string_view::npos) break;
                at = end + ((text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n') ? 2 : 1);
            }
            return out;
        }

        bool IsHex(std::string_view s)
        {
            return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
        }

        // (?:0x)?[0-9a-f]+ as the whole text
        std::optional<std::uint32_t> HexId(std::string_view s)
        {
            if (!IsHex(s) && s.size() > 2 && (s.starts_with("0x") || s.starts_with("0X"))) s.remove_prefix(2);
            if (!IsHex(s)) return std::nullopt;
            if (s.size() > kLocalHexDigits) s.remove_prefix(s.size() - kLocalHexDigits);
            return static_cast<std::uint32_t>(std::stoul(std::string(s), nullptr, 16)) & kLocalBits;
        }

        bool PluginName(std::string_view s)
        {
            if (s.size() < 5) return false;
            const auto ext = AsciiText::ToLower(s.substr(s.size() - 4));
            return ext == ".esp" || ext == ".esm" || ext == ".esl";
        }

        // The first `keyword\s*=\s*([^:]+)` in a line (case ignored), as Python's re.search finds it
        std::optional<std::string_view> Setting(std::string_view line, std::string_view keyword)
        {
            const std::string lower = AsciiText::ToLower(line);
            for (std::size_t at = lower.find(keyword); at != std::string::npos; at = lower.find(keyword, at + 1)) {
                std::size_t i = at + keyword.size();
                while (i < line.size() && kSpace.find(line[i]) != std::string_view::npos) ++i;
                if (i >= line.size() || line[i] != '=') continue;
                const std::size_t afterEquals = ++i;
                while (i < line.size() && kSpace.find(line[i]) != std::string_view::npos) ++i;
                const auto end = std::min(line.find(':', i), line.size());
                if (end > i) return line.substr(i, end - i);
                if (i > afterEquals) return line.substr(i - 1, 1);   // \s* gives a space back to [^:]+
            }
            return std::nullopt;
        }

        bool IsNoneText(std::string_view s) { return AsciiText::EqualsIgnoreCase(s, kNone); }

        void ReadSpid(std::string_view text, const std::string& source, NameTable& names, DistributionConfigs& out)
        {
            for (std::string_view line : Lines(text)) {
                line = Trim(line.substr(0, line.find(';')));
                const auto eq = line.find('=');
                if (eq == std::string_view::npos) continue;
                const std::string kind = AsciiText::ToLower(Trim(line.substr(0, eq)));
                if (kind != "item" && kind != "deathitem") continue;
                std::vector<std::string_view> f;
                for (const auto part : Split(line.substr(eq + 1), '|')) f.push_back(Trim(part));
                f.resize(std::max(f.size(), kSpidFields));
                RawSpidLine spid;
                spid.deathItem = kind == "deathitem";
                spid.chance = !f[6].empty() && !IsNoneText(f[6]) ? std::string(f[6]) : "100";
                spid.source = source;
                for (auto e : Split(f[2], ',')) {
                    e = Trim(e);
                    if (e.empty() || IsNoneText(e)) continue;
                    e.remove_prefix(std::min(e.find_first_not_of('-'), e.size()));
                    for (auto p : Split(e, '+')) {
                        if (!(p = Trim(p)).empty()) spid.formParts.push_back(ParseFormToken(p, names));
                    }
                }
                for (const bool form : { false, true }) {
                    for (auto e : Split(form ? f[2] : f[1], ',')) {
                        e = Trim(e);
                        if (e.empty() || IsNoneText(e) || e.starts_with('-')) continue;
                        for (auto p : Split(e, '+')) {
                            if ((p = Trim(p)).empty()) continue;
                            SpidFilterPart part{ form, {}, std::string(p) };
                            if (form) part.token = ParseFormToken(p, names);
                            spid.filters.push_back(std::move(part));
                        }
                    }
                }
                for (const auto spec : Split(f[0], ',')) spid.forms.push_back(ParseFormToken(spec, names));
                out.spid.push_back(std::move(spid));
            }
        }

        void ReadSkyPatcher(std::string_view text, NameTable& names, DistributionConfigs& out)
        {
            for (std::string_view line : Lines(text)) {
                line = line.substr(0, line.find(';'));
                const auto adds = Setting(line, "addtolls");
                if (!adds) continue;
                RawListAdd add;
                if (const auto lists = Setting(line, "filterbylls")) {
                    for (const auto t : Split(*lists, ',')) add.lists.push_back(ParseFormToken(t, names));
                }
                for (const auto spec : Split(*adds, ',')) {
                    std::vector<std::string_view> p;
                    for (const auto s : Split(spec, '~')) p.push_back(Trim(s));
                    const bool pluginForm = p.size() > 1 && PluginName(p[1]);
                    const std::string form = pluginForm ? std::string(p[0]) + "~" + std::string(p[1]) : std::string(p[0]);
                    add.forms.push_back(ParseFormToken(form, names));
                }
                out.listAdds.push_back(std::move(add));
            }
        }

        // FULL names from a localized plugin's string table (only the ids asked for)
        void LookUpNames(const DataFiles& data, const std::string& plugin, std::unordered_map<std::uint32_t, std::string>& ids)
        {
            const auto dot = plugin.rfind('.');
            std::vector<unsigned char> table;
            if (!data.Read("strings/" + plugin.substr(0, dot) + "_english.strings", table) || table.size() < kStringsHeader) return;
            std::uint32_t count;
            std::memcpy(&count, table.data(), 4);
            const std::size_t base = kStringsHeader + static_cast<std::size_t>(count) * kStringsEntry;
            if (base > table.size()) return;
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t id, offset;
                std::memcpy(&id, table.data() + kStringsHeader + i * kStringsEntry, 4);
                std::memcpy(&offset, table.data() + kStringsHeader + i * kStringsEntry + 4, 4);
                const auto it = ids.find(id);
                if (it == ids.end() || base + offset >= table.size()) continue;
                const char* s = reinterpret_cast<const char*>(table.data() + base + offset);
                it->second.assign(s, strnlen(s, table.size() - base - offset));
            }
        }

        // String filters that name an NPC (editor id or name) and no keyword of an NPC or its race
        std::unordered_set<std::string> NpcLabels(const DistributionConfigs& configs, const SpellStore& store,
            const DataFiles& data, const NameTable& names)
        {
            std::unordered_set<std::string> wanted;
            for (const auto& line : configs.spid) {
                for (const auto& f : line.filters) {
                    if (!f.form) wanted.insert(AsciiText::ToLower(f.text));
                }
            }
            std::unordered_set<std::string> npcNames, keywords;
            std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::string>> localized;
            const auto keyword = [&](Key k) {
                const auto it = store.keywordIds.find(k);
                if (it != store.keywordIds.end() && !it->second.empty()) {
                    if (auto lower = AsciiText::ToLower(it->second); wanted.contains(lower)) keywords.insert(std::move(lower));
                }
            };
            for (const auto& [key, npc] : store.npcs) {
                if (names.Name(PluginOf(key)) == "skyrim.esm" && LocalOf(key) == kPlayerLocal) continue;
                if (const auto e = store.editorIds.find(key); e != store.editorIds.end()) {
                    if (auto lower = AsciiText::ToLower(e->second); wanted.contains(lower)) npcNames.insert(std::move(lower));
                }
                if (npc.nameId) localized[npc.namePlugin][npc.nameId];
                else if (auto lower = AsciiText::ToLower(npc.name); !lower.empty() && wanted.contains(lower)) npcNames.insert(std::move(lower));
                for (const Key k : npc.keywords) keyword(k);
                if (const auto race = store.raceKeywords.find(npc.race); race != store.raceKeywords.end()) {
                    for (const Key k : race->second) keyword(k);
                }
            }
            for (auto& [plugin, ids] : localized) {
                LookUpNames(data, names.Name(plugin), ids);
                for (const auto& [_id, text] : ids) {
                    if (auto lower = AsciiText::ToLower(text); !lower.empty() && wanted.contains(lower)) npcNames.insert(std::move(lower));
                }
            }
            std::erase_if(npcNames, [&](const std::string& n) { return keywords.contains(n); });
            return npcNames;
        }

        FilterKind KindOf(const std::array<char, 4>* type)
        {
            if (!type) return FilterKind::kOther;
            const std::string_view t(type->data(), 4);
            if (t == "FACT") return FilterKind::kFaction;
            if (t == "RACE") return FilterKind::kRace;
            if (t == "KYWD") return FilterKind::kKeyword;
            if (t == "NPC_") return FilterKind::kNpc;
            return FilterKind::kOther;
        }

        const std::array<char, 4>* TypeOf(Key key, const SpellStore& store)
        {
            const auto it = store.tokenTypes.find(key);
            return key && it != store.tokenTypes.end() ? &it->second : nullptr;
        }
    }

    FormToken ParseFormToken(std::string_view text, NameTable& names)
    {
        const std::string_view tok = Trim(text);
        if (const auto tilde = tok.find('~'); tilde != std::string_view::npos) {
            const auto id = HexId(Trim(tok.substr(0, tilde)));
            const auto plugin = Trim(tok.substr(tilde + 1));
            if (id && PluginName(plugin)) return { MakeKey(names.Id(plugin), *id), {} };
        }
        if (const auto bar = tok.rfind('|'); bar != std::string_view::npos) {
            const auto plugin = Trim(tok.substr(0, bar));
            const auto id = HexId(Trim(tok.substr(bar + 1)));
            if (id && PluginName(plugin)) return { MakeKey(names.Id(plugin), *id), {} };
        }
        return { 0, AsciiText::ToLower(tok) };
    }

    ConfigWants DistributionConfigs::Wants() const
    {
        ConfigWants w;
        const auto add = [&](const FormToken& t) {
            if (t.key) w.keys.insert(t.key);
            else w.editorIds.insert(t.editorId);
        };
        for (const auto& line : spid) {
            for (const auto& t : line.forms) add(t);
            for (const auto& t : line.formParts) add(t);
            for (const auto& f : line.filters) {
                if (f.form) add(f.token);
                else w.strings.insert(AsciiText::ToLower(f.text));
            }
        }
        for (const auto& a : listAdds) {
            for (const auto& t : a.forms) add(t);
            for (const auto& t : a.lists) add(t);
        }
        return w;
    }

    DistributionConfigs ReadDistributionConfigs(const DataFiles& data, NameTable& names)
    {
        DistributionConfigs out;
        for (const auto& c : data.SpidConfigs()) {
            ReadSpid(ReadText(c.file), c.path, names, out);
            ++out.files;
        }
        for (const auto& c : data.SkyPatcherConfigs()) {
            ReadSkyPatcher(ReadText(c.file), names, out);
            ++out.files;
        }
        return out;
    }

    Key ResolveToken(const FormToken& token, const SpellStore& store)
    {
        if (token.key) return token.key;
        const auto it = store.tokenEditorIds.find(token.editorId);
        return it != store.tokenEditorIds.end() ? it->second : 0;
    }

    Distribution ResolveDistribution(const DistributionConfigs& configs, const SpellStore& store, const DataFiles& data,
        const NameTable& names)
    {
        Distribution out;
        const auto npcLabels = NpcLabels(configs, store, data, names);
        for (const auto& raw : configs.spid) {
            SpidLine line;
            line.deathItem = raw.deathItem;
            line.chance = raw.chance;
            line.source = raw.source;
            line.judged = std::all_of(raw.formParts.begin(), raw.formParts.end(), [&](const FormToken& t) {
                return KindOf(TypeOf(ResolveToken(t, store), store)) != FilterKind::kOther;
            });
            for (const auto& f : raw.filters) {
                if (f.form) line.filters.push_back(KindOf(TypeOf(ResolveToken(f.token, store), store)));
                else line.filters.push_back(npcLabels.contains(AsciiText::ToLower(f.text)) ? FilterKind::kNpc : FilterKind::kString);
            }
            for (const auto& t : raw.forms) {
                if (const Key k = ResolveToken(t, store)) {
                    line.form = k;
                    out.spid.push_back(line);
                }
            }
        }
        for (const auto& raw : configs.listAdds) {
            ListAdd add;
            for (const auto& t : raw.lists) {
                if (const Key k = ResolveToken(t, store)) add.lists.push_back(k);
            }
            for (const auto& t : raw.forms) {
                if (const Key k = ResolveToken(t, store)) {
                    add.form = k;
                    out.listAdds.push_back(add);
                }
            }
        }
        return out;
    }

    SpidClass ClassOf(const SpidLine& line)
    {
        const auto has = [&](FilterKind k) { return std::find(line.filters.begin(), line.filters.end(), k) != line.filters.end(); };
        const auto only = [&](std::initializer_list<FilterKind> ks) {
            return std::all_of(line.filters.begin(), line.filters.end(), [&](FilterKind f) {
                return std::find(ks.begin(), ks.end(), f) != ks.end();
            });
        };
        if (!line.judged) return { "other", "spid" };
        if (has(FilterKind::kFaction)) return { "faction", "spid faction" };
        if (!line.filters.empty() && only({ FilterKind::kNpc })) return { "encounter", "spid npc" };
        if (line.filters.empty()) return { "generic", "spid all" };
        return { "generic", only({ FilterKind::kRace }) ? "spid race" : "spid npc type" };
    }
}
