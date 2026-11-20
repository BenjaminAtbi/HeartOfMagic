#include "tomereach/TomeReachCdf.h"
#include "AsciiText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>

namespace TomeReach
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::string_view kBom = "\xEF\xBB\xBF";
        constexpr std::string_view kBetweenRules = " \t\r\n,";
        constexpr std::string_view kSpace = " \t\r\n";

        // Python's truth value of a JSON value
        bool Truthy(const Json& v)
        {
            if (v.is_null()) return false;
            if (v.is_boolean()) return v.get<bool>();
            if (v.is_number_integer()) return v.get<std::int64_t>() != 0;
            if (v.is_number()) return v.get<double>() != 0.0;
            if (v.is_string()) return !v.get_ref<const std::string&>().empty();
            return !v.empty();
        }

        const Json* Get(const Json& object, const char* key)
        {
            if (!object.is_object()) return nullptr;
            const auto it = object.find(key);
            return it == object.end() || it->is_null() ? nullptr : &*it;
        }

        // The end of the JSON value starting at `at` (an object or array, brackets matched outside strings);
        // nullopt when the text ends first or the value is a scalar
        std::optional<std::size_t> ValueEnd(std::string_view text, std::size_t at)
        {
            if (text[at] != '{' && text[at] != '[') return std::nullopt;
            int depth = 0;
            bool inString = false;
            for (std::size_t i = at; i < text.size(); ++i) {
                const char c = text[i];
                if (inString) {
                    if (c == '\\') ++i;
                    else if (c == '"') inString = false;
                } else if (c == '"') {
                    inString = true;
                } else if (c == '{' || c == '[') {
                    ++depth;
                } else if ((c == '}' || c == ']') && --depth == 0) {
                    return i + 1;
                }
            }
            return std::nullopt;
        }

        // `"rules"\s*:\s*\[`: the position after the bracket
        std::optional<std::size_t> RulesStart(std::string_view text)
        {
            constexpr std::string_view kKey = "\"rules\"";
            for (std::size_t at = text.find(kKey); at != std::string_view::npos; at = text.find(kKey, at + 1)) {
                std::size_t i = text.find_first_not_of(kSpace, at + kKey.size());
                if (i == std::string_view::npos || text[i] != ':') continue;
                i = text.find_first_not_of(kSpace, i + 1);
                if (i != std::string_view::npos && text[i] == '[') return i + 1;
            }
            return std::nullopt;
        }

        // cdf_parse: the rules, and the line of a syntax error (0: none); after an error, the whole rules before it
        std::vector<Json> ParseRules(std::string_view text, std::size_t& errorLine)
        {
            errorLine = 0;
            if (text.starts_with(kBom)) text.remove_prefix(kBom.size());
            try {
                const Json d = Json::parse(text);
                const Json* rules = Get(d, "rules");
                return rules && rules->is_array() ? rules->get<std::vector<Json>>() : std::vector<Json>{};
            } catch (const Json::parse_error& e) {
                const std::size_t upto = std::min<std::size_t>(e.byte ? e.byte - 1 : 0, text.size());
                errorLine = 1 + static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(upto), '\n'));
            }
            std::vector<Json> rules;
            const auto start = RulesStart(text);
            if (!start) return rules;
            for (std::size_t i = *start;;) {
                i = text.find_first_not_of(kBetweenRules, i);
                if (i == std::string_view::npos || text[i] == ']') break;
                const auto end = ValueEnd(text, i);
                if (!end) break;
                Json rule = Json::parse(text.substr(i, *end - i), nullptr, false);
                if (rule.is_discarded()) break;
                rules.push_back(std::move(rule));
                i = *end;
            }
            return rules;
        }

        std::string Text(const Json& v) { return v.is_string() ? v.get<std::string>() : v.dump(); }

        void Strings(const Json* v, NameTable& names, std::vector<FormToken>& out)
        {
            if (!v || !v->is_array()) return;
            for (const auto& x : *v) {
                if (x.is_string()) out.push_back(ParseFormToken(x.get_ref<const std::string&>(), names));
            }
        }
    }

    void ReadCdf(std::string_view text, const std::string& source, const std::unordered_set<std::string>& activePlugins,
        NameTable& names, std::vector<RawCdfChange>& out, std::vector<std::string>& warnings)
    {
        std::size_t errorLine = 0;
        const auto rules = ParseRules(text, errorLine);
        if (errorLine) {
            warnings.push_back(source + ": syntax error at line " + std::to_string(errorLine) + "; CDF reads only the " +
                std::to_string(rules.size()) + " rules before it");
        }
        for (const auto& r : rules) {
            const Json* name = Get(r, "friendlyName");
            const Json* changes = Get(r, "changes");
            if (!name || !name->is_string() || !changes || !changes->is_array()) {
                warnings.push_back(source + ": rule " + (name ? Text(*name) : std::string("None")) +
                    " lacks friendlyName or changes; CDF stops reading the file there");
                break;
            }
            const std::string rule = name->get<std::string>();
            const Json* cond = Get(r, "conditions");
            static const Json kEmpty = Json::object();
            if (!cond || !cond->is_object()) cond = &kEmpty;
            std::string missing;
            if (const Json* plugins = Get(*cond, "plugins"); plugins && plugins->is_array()) {
                for (const auto& p : *plugins) {
                    if (!activePlugins.contains(AsciiText::ToLower(Text(p)))) missing += (missing.empty() ? "" : ", ") + Text(p);
                }
            }
            if (!missing.empty()) {
                warnings.push_back(source + ": [" + rule + "] needs " + missing + "; CDF stops reading the file there");
                break;
            }
            RawCdfChange base;
            base.rule = rule;
            base.source = source;
            Strings(Get(*cond, "containers"), names, base.containers);
            const Json* allow = Get(*cond, "allowVendors");
            const Json* only = Get(*cond, "onlyVendors");
            base.vendors = (allow && Truthy(*allow)) || (only && Truthy(*only));
            for (const auto& ch : *changes) {
                if (!ch.is_object()) continue;
                RawCdfChange c = base;
                if (const Json* add = Get(ch, "add")) {
                    if (!add->is_array()) {
                        warnings.push_back(source + ": [" + rule + "] an add entry doesn't resolve; change dropped");
                        continue;
                    }
                    c.hasAdd = true;
                    c.addEntries = add->size();
                    Strings(add, names, c.add);
                }
                if (const Json* rm = Get(ch, "remove")) {
                    if (!rm->is_string()) {
                        warnings.push_back(source + ": [" + rule + "] remove is not a single string; change dropped");
                        continue;
                    }
                    if (!rm->get_ref<const std::string&>().empty()) {
                        c.hasRemove = true;
                        c.remove = ParseFormToken(rm->get_ref<const std::string&>(), names);
                    }
                }
                const Json* rmkw = Get(ch, "removeByKeywords");
                c.removeKeywords = rmkw && Truthy(*rmkw);
                c.countGiven = Get(ch, "count") != nullptr;
                if (!c.hasRemove && !c.removeKeywords && c.addEntries == 0) continue;
                out.push_back(std::move(c));
            }
        }
    }

    std::vector<CdfChange> ResolveCdf(const std::vector<RawCdfChange>& raw, const SpellStore& store,
        std::vector<std::string>& warnings)
    {
        std::vector<CdfChange> out;
        for (const auto& r : raw) {
            CdfChange c;
            c.rule = r.rule;
            c.vendors = r.vendors;
            c.countGiven = r.countGiven;
            for (const auto& t : r.add) {
                if (const Key k = ResolveToken(t, store)) c.add.push_back(k);
            }
            if (c.add.size() != r.addEntries) {
                warnings.push_back(r.source + ": [" + r.rule + "] an add entry doesn't resolve; change dropped");
                continue;
            }
            if (r.hasRemove) {
                const Key k = ResolveToken(r.remove, store);
                if (!k) {
                    warnings.push_back(r.source + ": [" + r.rule + "] remove " + r.remove.editorId + " doesn't resolve; change dropped");
                    continue;
                }
                c.remove.push_back(k);
                c.op = c.add.empty() ? CdfOp::kRemove : CdfOp::kReplace;
            } else if (r.removeKeywords) {
                c.op = c.add.empty() ? CdfOp::kRemoveKeywords : CdfOp::kReplaceKeywords;
            } else if (!c.add.empty()) {
                c.op = CdfOp::kAdd;
            } else {
                continue;
            }
            for (const auto& t : r.containers) {
                if (const Key k = ResolveToken(t, store)) c.containers.push_back(k);
            }
            out.push_back(std::move(c));
        }
        return out;
    }
}
