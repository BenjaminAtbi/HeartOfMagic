#include "tomereach/TomeReachRecords.h"
#include "tomereach/TomeReachPlugin.h"

#include <algorithm>

namespace TomeReach
{
    // =============================================================================
    // NAMES
    // =============================================================================

    std::uint32_t NameTable::Id(std::string_view fileName)
    {
        auto lower = AsciiText::ToLower(fileName);
        if (auto it = m_ids.find(lower); it != m_ids.end()) return it->second;
        const auto id = static_cast<std::uint32_t>(m_names.size());
        const bool eslName = lower.size() > 4 && lower.ends_with(".esl");
        m_ids.emplace(lower, id);
        m_names.push_back(std::move(lower));
        m_light.push_back(eslName);
        return id;
    }

    void NameTable::SetLight(std::uint32_t id, bool light)
    {
        if (id < m_light.size()) m_light[id] = m_light[id] || light;
    }

    bool NameTable::IsLight(std::uint32_t id) const
    {
        return id < m_light.size() && m_light[id];
    }

    // =============================================================================
    // RECORDS
    // =============================================================================

    namespace
    {
        constexpr std::uint8_t kTeachesSpell = 0x04;
        constexpr std::size_t kFormIdSize = 4;
        constexpr std::size_t kLevelEntryFormOffset = 4;   // LVLO: level, pad, form, count
        constexpr std::size_t kBookSpellOffset = 4;        // BOOK DATA: flags, type, pad, spell

        // The record types whose items or script properties can hand out a tome
        constexpr std::array kScanned = {
            T("BOOK"), T("REFR"), T("ACHR"), T("LVLI"), T("CONT"), T("NPC_"), T("FLST"), T("COBJ"),
            T("QUST"), T("INFO"), T("ACTI"), T("PERK"), T("SCEN"), T("PACK"), T("MGEF"), T("SPEL"),
            T("MISC"), T("ALCH"), T("ARMO"), T("WEAP"), T("FURN"), T("DOOR"), T("FLOR"), T("TACT"),
            T("LIGH"), T("ENCH"), T("KEYM"), T("AMMO"), T("SCRL"), T("INGR"), T("SLGM"),
        };

        bool Scanned(const unsigned char* type)
        {
            return std::any_of(kScanned.begin(), kScanned.end(), [&](const Tag& t) { return Is(type, t); });
        }

        struct Collector
        {
            const Resolver& resolve;
            const std::unordered_set<Key>& wanted;
            Evidence& evidence;

            // `quest`: the hit is a quest's (Evidence::foundQuest)
            void Add(std::uint32_t formId, bool loose = false, bool quest = false) const
            {
                if (!formId) return;
                const Key key = resolve(formId);
                if (!wanted.contains(key)) return;
                if (quest) {
                    evidence.foundQuest.insert(key);
                    // Plugins are read one after another: one entry per plugin
                    auto& from = evidence.questFrom[key];
                    if (from.empty() || from.back() != resolve.self) from.push_back(resolve.self);
                }
                if (!loose) {
                    evidence.found.insert(key);
                    if (!quest) evidence.foundOtherField.insert(key);
                    return;
                }
                if (!quest) evidence.foundOtherLoose.insert(key);
                if (evidence.foundLoose.insert(key).second) evidence.looseFrom.emplace(key, resolve.self);
            }

            // A script property or quest alias: any four bytes may be a form id (a
            // false hit can only keep a tome, never drop one). `quest`: the record
            // is a quest's (a QUST, or the dialogue or scene whose fragments are its
            // script), whose hits alone can make a tome quest-only
            void AddLoose(const unsigned char* data, std::size_t size, bool quest) const
            {
                for (std::size_t i = 0; i + kFormIdSize <= size; ++i) Add(U32(data + i), true, quest);
            }
        };

        // False when a subrecord runs past the record: the rest of it was not read
        bool ReadRecord(const unsigned char* type, const unsigned char* body, std::size_t size,
            std::uint32_t formId, const Collector& collect, std::vector<BookRecord>* books)
        {
            const bool quest = Is(type, T("QUST"));
            // A quest's script fragments also live in its dialogue (INFO) and scenes (SCEN);
            // a package's (PACK) belong to an AI package, not to a quest
            const bool questScript = quest || Is(type, T("INFO")) || Is(type, T("SCEN"));
            BookRecord book;
            const bool isBook = books && Is(type, T("BOOK"));
            const bool whole = ForEachSubrecord(body, size, [&](const Subrecord& sub) {
                if (Is(type, T("REFR")) && Is(sub.type, T("NAME")) && sub.size >= 4) {
                    collect.Add(U32(sub.data));
                } else if (Is(type, T("LVLI")) && Is(sub.type, T("LVLO")) && sub.size >= kLevelEntryFormOffset + 4) {
                    collect.Add(U32(sub.data + kLevelEntryFormOffset));
                } else if (Is(sub.type, T("CNTO")) && sub.size >= 4) {
                    collect.Add(U32(sub.data), false, quest);  // a QUST's: an alias's inventory
                } else if (Is(type, T("FLST")) && Is(sub.type, T("LNAM")) && sub.size >= 4) {
                    collect.Add(U32(sub.data));
                } else if (Is(type, T("COBJ")) && Is(sub.type, T("CNAM")) && sub.size >= 4) {
                    collect.Add(U32(sub.data));
                } else if (Is(sub.type, T("VMAD")) ||
                           (quest && (Is(sub.type, T("ALCO")) || Is(sub.type, T("ALFR")) || Is(sub.type, T("ALUA"))))) {
                    collect.AddLoose(sub.data, sub.size, questScript);
                }
                if (isBook) {
                    if (Is(sub.type, T("EDID"))) {
                        book.editorId = ZString(sub);
                    } else if (Is(sub.type, T("DATA")) && sub.size >= kBookSpellOffset + 4 &&
                               (sub.data[0] & kTeachesSpell)) {
                        book.spell = collect.resolve(U32(sub.data + kBookSpellOffset));
                    }
                }
            });
            if (isBook) {
                book.book = collect.resolve(formId);
                books->push_back(std::move(book));
            }
            return whole;
        }
    }

    PluginScan ScanPlugin(const PluginFile& file, NameTable& names, const std::unordered_set<Key>& wanted,
        Evidence& evidence, std::vector<BookRecord>* books)
    {
        return WalkPlugin(file, names, Scanned, [&](const RecordView& record, const Resolver& resolve) {
            const Collector collect{ resolve, wanted, evidence };
            return ReadRecord(record.type, record.body, record.size, record.formId, collect, books);
        });
    }
}
