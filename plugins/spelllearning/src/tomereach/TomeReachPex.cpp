#include "tomereach/TomeReachPex.h"
#include "AsciiText.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace TomeReach
{
    namespace
    {
        constexpr std::array<unsigned char, 4> kMagic = { 0xFA, 0x57, 0xC0, 0xDE };

        // Opcodes and their fixed argument counts (pexdis.py OPS)
        enum Op : std::uint8_t
        {
            kNop = 0, kAssign = 13, kCast = 14, kJmp = 20, kJmpT = 21, kJmpF = 22, kCallMethod = 23, kCallParent = 24,
            kCallStatic = 25, kReturn = 26, kPropGet = 28, kPropSet = 29, kArrayGetElement = 32, kArraySetElement = 33,
        };
        constexpr std::array<std::uint8_t, 36> kArgCounts = {
            0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 1, 2, 2, 3, 2, 3, 1, 3, 3, 3, 2, 2, 3, 3, 4, 4,
        };
        // Value types
        constexpr std::uint8_t kNull = 0, kIdentifier = 1, kString = 2, kInteger = 3, kFloat = 4, kBool = 5;
        // Property flags: 4 an auto variable, else 1 a getter and 2 a setter function
        constexpr std::uint8_t kPropertyAuto = 4, kPropertyGetter = 1, kPropertySetter = 2;
        constexpr std::size_t kPasses = 3;   // loops: a value tainted late can feed an earlier instruction

        constexpr std::array<std::string_view, 3> kAddToList = { "addform", "addforms", "addtolist" };
        constexpr std::array<std::string_view, 1> kAddItem = { "additem" };
        // Calls whose result is not the list's form even when an argument is (an index, a size)
        constexpr std::array<std::string_view, 3> kIndexCalls = { "getat", "find", "getsize" };

        class Reader
        {
        public:
            Reader(const unsigned char* data, std::size_t size) : m_data(data), m_size(size) {}

            bool Ok() const { return m_ok; }

            std::uint64_t U(std::size_t bytes)
            {
                if (!m_ok || bytes > m_size - m_at) {
                    m_ok = false;
                    return 0;
                }
                std::uint64_t v = 0;
                for (std::size_t i = 0; i < bytes; ++i) v = (v << 8) | m_data[m_at + i];
                m_at += bytes;
                return v;
            }

            std::string WString()
            {
                const auto n = static_cast<std::size_t>(U(2));
                if (!m_ok) return {};
                const std::size_t take = std::min(n, m_size - m_at);
                std::string s(reinterpret_cast<const char*>(m_data + m_at), take);
                m_at += take;
                return s;
            }

        private:
            const unsigned char* m_data;
            std::size_t m_size;
            std::size_t m_at = 0;
            bool m_ok = true;
        };

        class Disassembler
        {
        public:
            Disassembler(const unsigned char* data, std::size_t size) : m_r(data, size) {}

            bool Run(std::vector<PexFunction>& out)
            {
                if (m_r.U(4) != 0xFA57C0DE) return false;
                m_r.U(1), m_r.U(1), m_r.U(2), m_r.U(8);
                m_r.WString(), m_r.WString(), m_r.WString();
                const auto count = m_r.U(2);
                for (std::uint64_t i = 0; i < count && m_r.Ok(); ++i) m_strings.push_back(m_r.WString());
                if (m_r.U(1)) {
                    m_r.U(8);
                    const auto functions = m_r.U(2);
                    for (std::uint64_t i = 0; i < functions && m_r.Ok(); ++i) {
                        m_r.U(2), m_r.U(2), m_r.U(2), m_r.U(1);
                        const auto lines = m_r.U(2);
                        for (std::uint64_t k = 0; k < lines && m_r.Ok(); ++k) m_r.U(2);
                    }
                }
                const auto userFlags = m_r.U(2);
                for (std::uint64_t i = 0; i < userFlags && m_r.Ok(); ++i) m_r.U(2), m_r.U(1);
                const auto objects = m_r.U(2);
                for (std::uint64_t o = 0; o < objects && m_r.Ok(); ++o) {
                    Str(), m_r.U(4), Str(), Str(), m_r.U(4), Str();
                    const auto variables = m_r.U(2);
                    for (std::uint64_t v = 0; v < variables && m_r.Ok(); ++v) {
                        Str(), Str(), m_r.U(4);
                        if (!Value()) return false;
                    }
                    const auto properties = m_r.U(2);
                    for (std::uint64_t p = 0; p < properties && m_r.Ok(); ++p) {
                        const std::string name = Str();
                        Str(), Str(), m_r.U(4);
                        const auto flags = static_cast<std::uint8_t>(m_r.U(1));
                        if (flags & kPropertyAuto) {
                            Str();
                            continue;
                        }
                        if ((flags & kPropertyGetter) && !Function(name + ".get", out)) return false;
                        if ((flags & kPropertySetter) && !Function(name + ".set", out)) return false;
                    }
                    const auto states = m_r.U(2);
                    for (std::uint64_t s = 0; s < states && m_r.Ok(); ++s) {
                        Str();
                        const auto functions = m_r.U(2);
                        for (std::uint64_t f = 0; f < functions && m_r.Ok(); ++f) {
                            if (!Function(Str(), out)) return false;
                        }
                    }
                }
                return m_r.Ok() && !m_failed;
            }

        private:
            Reader m_r;
            std::vector<std::string> m_strings;

            bool m_failed = false;   // a string index past the table

            std::string Str()
            {
                const auto i = static_cast<std::size_t>(m_r.U(2));
                if (i < m_strings.size()) return m_strings[i];
                m_failed = true;
                return {};
            }

            // A value as pexdis.py writes it; nullopt for an unknown type
            std::optional<std::string> Value(std::int64_t* integer = nullptr)
            {
                const auto type = static_cast<std::uint8_t>(m_r.U(1));
                switch (type) {
                case kNull: return std::string("None");
                case kIdentifier: return Str();
                case kString: return "'" + Str() + "'";
                case kInteger: {
                    const auto v = static_cast<std::int32_t>(static_cast<std::uint32_t>(m_r.U(4)));
                    if (integer) *integer = v;
                    return std::to_string(v);
                }
                case kFloat: {
                    const auto bits = static_cast<std::uint32_t>(m_r.U(4));
                    float f;
                    std::memcpy(&f, &bits, sizeof(f));
                    char text[32];
                    std::snprintf(text, sizeof(text), "%g", static_cast<double>(f));
                    return std::string(text);
                }
                case kBool: return std::string(m_r.U(1) ? "True" : "False");
                default: return std::nullopt;
                }
            }

            bool Function(std::string name, std::vector<PexFunction>& out)
            {
                PexFunction fn;
                fn.name = AsciiText::ToLower(name);
                Str(), Str(), m_r.U(4), m_r.U(1);
                const auto params = m_r.U(2);
                for (std::uint64_t i = 0; i < params && m_r.Ok(); ++i) {
                    fn.params.push_back(AsciiText::ToLower(Str()));
                    Str();
                }
                const auto locals = m_r.U(2);
                for (std::uint64_t i = 0; i < locals && m_r.Ok(); ++i) Str(), Str();
                const auto count = m_r.U(2);
                for (std::uint64_t i = 0; i < count && m_r.Ok(); ++i) {
                    const auto op = static_cast<std::uint8_t>(m_r.U(1));
                    if (op >= kArgCounts.size()) return false;
                    std::vector<std::string> args;
                    for (std::uint8_t a = 0; a < kArgCounts[op]; ++a) {
                        auto v = Value();
                        if (!v) return false;
                        args.push_back(std::move(*v));
                    }
                    if (op == kCallMethod || op == kCallParent || op == kCallStatic) {
                        std::int64_t extra = -1;
                        if (!Value(&extra) || extra < 0) return false;
                        for (std::int64_t a = 0; a < extra; ++a) {
                            auto v = Value();
                            if (!v) return false;
                            args.push_back(std::move(*v));
                        }
                    }
                    if (op == kJmp || op == kJmpT || op == kJmpF) args.back() = "->";
                    Split(op, args, fn.code);
                }
                if (!m_r.Ok() || m_failed) return false;
                out.push_back(std::move(fn));
                return true;
            }

            // The Python reads the arguments back from the text "a, b, c": split it the same way
            static void Split(std::uint8_t op, const std::vector<std::string>& args, std::vector<PexInstruction>& code)
            {
                std::string text;
                for (std::size_t i = 0; i < args.size(); ++i) text += (i ? ", " : "") + args[i];
                const auto first = text.find_first_not_of(" \t\r\n\v\f");
                if (first == std::string::npos) return;
                PexInstruction ins{ op, {} };
                std::size_t at = first;
                while (true) {
                    const auto comma = text.find(", ", at);
                    std::string part = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                    const auto b = part.find_first_not_of(" \t\r\n\v\f");
                    const auto e = part.find_last_not_of(" \t\r\n\v\f");
                    ins.args.push_back(b == std::string::npos ? std::string() : AsciiText::ToLower(part.substr(b, e - b + 1)));
                    if (comma == std::string::npos) break;
                    at = comma + 2;
                }
                code.push_back(std::move(ins));
            }
        };

        // The functions with every argument, parameter and function name as a number (one per text)
        struct Coded
        {
            struct Instruction
            {
                std::uint8_t op;
                std::vector<int> args;
            };

            struct Body
            {
                int name;
                std::vector<int> params;
                std::vector<Instruction> code;
                std::vector<int> tokens;   // every argument it names, sorted
            };

            std::vector<Body> bodies;
            std::unordered_map<std::string, int> ids;

            int Id(const std::string& text)
            {
                return ids.try_emplace(text, static_cast<int>(ids.size())).first->second;
            }

            int Find(std::string_view text) const
            {
                const auto it = ids.find(std::string(text));
                return it != ids.end() ? it->second : -1;
            }

            explicit Coded(const std::vector<PexFunction>& functions)
            {
                for (const auto& fn : functions) {
                    Body body{ Id(fn.name), {}, {}, {} };
                    for (const auto& p : fn.params) body.params.push_back(Id(p));
                    for (const auto& ins : fn.code) {
                        Instruction coded{ ins.op, {} };
                        for (const auto& a : ins.args) coded.args.push_back(Id(a));
                        body.tokens.insert(body.tokens.end(), coded.args.begin(), coded.args.end());
                        body.code.push_back(std::move(coded));
                    }
                    std::sort(body.tokens.begin(), body.tokens.end());
                    body.tokens.erase(std::unique(body.tokens.begin(), body.tokens.end()), body.tokens.end());
                    bodies.push_back(std::move(body));
                }
            }
        };

        // granted_properties: does a value seeded in `seeds` reach a call of one of `targets`
        class Flow
        {
        public:
            template <std::size_t N>
            Flow(const Coded& coded, const std::array<std::string_view, N>& targets) :
                m_coded(coded), m_self(coded.Find("self"))
            {
                for (const auto t : targets) m_targets.push_back(coded.Find(t));
                for (const auto t : kIndexCalls) m_indexCalls.push_back(coded.Find(t));
                for (std::size_t i = 0; i < coded.bodies.size(); ++i) {
                    auto [it, added] = m_byName.try_emplace(coded.bodies[i].name);
                    if (added) m_names.push_back(coded.bodies[i].name);
                    it->second.push_back(i);
                }
            }

            // The function names in first-seen order, each with its bodies (objects and states)
            const std::vector<int>& Names() const { return m_names; }
            const std::vector<std::size_t>& Bodies(int name) const { return m_byName.at(name); }

            bool Reaches(std::size_t body, std::vector<int> seeds)
            {
                std::sort(seeds.begin(), seeds.end());
                seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
                // A body that never names a seed cannot pass it on
                const auto& tokens = m_coded.bodies[body].tokens;
                if (std::none_of(seeds.begin(), seeds.end(), [&](int s) { return std::binary_search(tokens.begin(), tokens.end(), s); }))
                    return false;
                std::vector<int> key{ static_cast<int>(body) };
                key.insert(key.end(), seeds.begin(), seeds.end());
                if (const auto it = m_memo.find(key); it != m_memo.end()) return it->second;
                m_memo[key] = false;   // a call back into this body while it runs reaches nothing more
                const bool result = Run(body, seeds);
                m_memo[key] = result;
                return result;
            }

        private:
            const Coded& m_coded;
            int m_self;
            std::vector<int> m_targets;
            std::vector<int> m_indexCalls;
            std::unordered_map<int, std::vector<std::size_t>> m_byName;
            std::vector<int> m_names;
            std::map<std::vector<int>, bool> m_memo;

            static bool In(const std::vector<int>& set, int id) { return id >= 0 && std::find(set.begin(), set.end(), id) != set.end(); }

            bool Run(std::size_t body, const std::vector<int>& seeds)
            {
                std::unordered_set<int> tainted(seeds.begin(), seeds.end());
                const auto put = [&](int dest, bool hot) {
                    // The compiler reuses temporaries: an untainted write clears the variable (seeds stay)
                    if (hot) tainted.insert(dest);
                    else if (!std::binary_search(seeds.begin(), seeds.end(), dest)) tainted.erase(dest);
                };
                std::vector<std::size_t> hot;
                const auto hotIn = [&](const std::vector<int>& a, std::size_t from) {
                    hot.clear();
                    for (std::size_t i = from; i < a.size(); ++i) {
                        if (tainted.contains(a[i])) hot.push_back(i - from);
                    }
                    return !hot.empty();
                };
                for (std::size_t pass = 0; pass < kPasses; ++pass) {
                    for (const auto& ins : m_coded.bodies[body].code) {
                        const auto& a = ins.args;
                        if (ins.op == kCallMethod && a.size() >= 3) {
                            const bool anyHot = hotIn(a, 3);
                            if (anyHot && In(m_targets, a[0])) return true;
                            if (anyHot && a[1] == m_self) {
                                if (const auto callee = m_byName.find(a[0]); callee != m_byName.end()) {
                                    const auto calleeHot = hot;
                                    for (const std::size_t j : callee->second) {
                                        const auto& params = m_coded.bodies[j].params;
                                        std::vector<int> sub;
                                        for (const std::size_t i : calleeHot) {
                                            if (i < params.size()) sub.push_back(params[i]);
                                        }
                                        if (!sub.empty() && Reaches(j, sub)) return true;
                                    }
                                }
                            }
                            put(a[2], tainted.contains(a[1]) || (anyHot && !In(m_indexCalls, a[0])));
                        } else if (ins.op == kCallStatic && a.size() >= 3) {
                            const bool anyHot = hotIn(a, 3);
                            if (anyHot && In(m_targets, a[1])) return true;
                            put(a[2], anyHot);
                        } else if (ins.op == kArrayGetElement && a.size() >= 2) {
                            put(a[0], tainted.contains(a[1]));
                        } else if ((ins.op == kCast || ins.op == kAssign) && a.size() >= 2) {
                            put(a[0], tainted.contains(a[1]));
                        } else if (ins.op == kPropGet && a.size() >= 3) {
                            put(a[2], tainted.contains(a[1]));
                        } else if (ins.op != kJmp && ins.op != kJmpT && ins.op != kJmpF && ins.op != kReturn &&
                                   ins.op != kNop && ins.op != kPropSet && ins.op != kArraySetElement &&
                                   ins.op != kCallParent && !a.empty()) {
                            put(a[0], false);   // arithmetic, comparisons, strcat, array_length...: not a form
                        }
                    }
                }
                return false;
            }
        };

        template <std::size_t N>
        std::unordered_set<std::string> Granted(const Coded& coded, const std::array<std::string_view, N>& targets,
            const std::vector<std::string>& properties)
        {
            Flow flow(coded, targets);
            std::unordered_set<std::string> out;
            for (const auto& p : properties) {
                const std::vector<int> seed{ coded.Find("::" + p + "_var") };
                bool reached = false;
                for (const int name : flow.Names()) {
                    for (const std::size_t body : flow.Bodies(name)) {
                        if ((reached = flow.Reaches(body, seed))) break;
                    }
                    if (reached) break;
                }
                if (reached) out.insert(p);
            }
            return out;
        }
    }

    bool DisassemblePex(const std::vector<unsigned char>& data, std::vector<PexFunction>& out)
    {
        // Files from archives with embedded names carry the path in front
        const auto start = std::search(data.begin(), data.end(), kMagic.begin(), kMagic.end());
        const std::size_t offset = start == data.end() ? 0 : static_cast<std::size_t>(start - data.begin());
        return Disassembler(data.data() + offset, data.size() - offset).Run(out);
    }

    ScriptGrants AnalyseScript(const std::vector<unsigned char>& data)
    {
        std::vector<PexFunction> functions;
        if (!DisassemblePex(data, functions)) return {};
        // Properties: every auto variable an instruction names ("::<property>_var")
        std::vector<std::string> properties;
        for (const auto& fn : functions) {
            for (const auto& ins : fn.code) {
                for (const auto& a : ins.args) {
                    if (a.size() >= 6 && a.starts_with("::") && a.ends_with("_var")) properties.push_back(a.substr(2, a.size() - 6));
                }
            }
        }
        std::sort(properties.begin(), properties.end());
        properties.erase(std::unique(properties.begin(), properties.end()), properties.end());
        const Coded coded(functions);
        return ScriptGrants{ Granted(coded, kAddToList, properties), Granted(coded, kAddItem, properties) };
    }
}
