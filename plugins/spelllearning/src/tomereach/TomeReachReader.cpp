#include "tomereach/TomeReachReader.h"
#include "tomereach/TomeReachPlugin.h"
#include "tomereach/TomeReachScripts.h"
#include "AsciiText.h"

#include <algorithm>
#include <cstring>
#include <tuple>
#include <optional>
#include <unordered_set>

namespace TomeReach
{
    namespace
    {
        constexpr std::size_t kFormSize = 4;
        constexpr std::size_t kLevelEntryFormOffset = 4;   // LVLO: level, pad, form, count
        constexpr std::size_t kBookSpellOffset = 4;        // BOOK DATA: flags, type, pad, spell
        constexpr std::uint8_t kTeachesSpell = 0x04;
        constexpr std::uint8_t kStaffAnimation = 8;         // WEAP DNAM: the animation type, first byte
        constexpr std::uint8_t kInteriorCell = 0x01;        // CELL DATA: flags, first byte

        constexpr std::array kRead = {
            T("BOOK"), T("SCRL"), T("WEAP"), T("LVLI"), T("LVLN"), T("CONT"), T("NPC_"), T("OTFT"),
            T("FACT"), T("COBJ"), T("QUST"), T("CELL"), T("LCTN"), T("WRLD"), T("REFR"), T("ACHR"),
            // Read for their scripts, form lists, keywords, or only for the editor ids the configs name
            T("SPEL"), T("ENCH"), T("MGEF"), T("ARMO"), T("LVSP"), T("PERK"), T("AVIF"), T("RACE"), T("KYWD"),
            T("FLST"), T("HAZD"), T("EXPL"), T("PROJ"), T("SHOU"), T("WOOP"), T("ACTI"), T("GLOB"), T("MISC"),
            T("ALCH"),
        };

        // Records whose scripts can hand items out (scripts/spellmap/reach.py HOLDERS)
        constexpr std::array kHolderTypes = {
            T("QUST"), T("ACTI"), T("MGEF"), T("PERK"), T("MISC"), T("BOOK"), T("CONT"), T("NPC_"), T("ALCH"),
            T("WEAP"), T("ARMO"),
        };

        bool Wanted(const unsigned char* type)
        {
            return std::any_of(kRead.begin(), kRead.end(), [&](const Tag& t) { return Is(type, t); });
        }

        bool PlacedKind(Kind kind)
        {
            return kind == Kind::kContainer || kind == Kind::kNpc || kind == Kind::kNpcList || kind == Kind::kBook ||
                   kind == Kind::kScroll || kind == Kind::kStaff;
        }

        // Record types whose references may be kept. A record keeps its type in every
        // override, and a reference's base is read before it (in a master, or earlier
        // in the same plugin), so a reference to any other type is never needed. Whether
        // a BOOK teaches or a WEAP is a staff is known only from the winning version:
        // ReadSpellStore checks the kind once every plugin is read.
        constexpr std::array kPlaceableTypes = { T("BOOK"), T("SCRL"), T("WEAP"), T("LVLN"), T("CONT"), T("NPC_") };

        bool PlaceableType(const unsigned char* type)
        {
            return std::any_of(kPlaceableTypes.begin(), kPlaceableTypes.end(), [&](const Tag& t) { return Is(type, t); });
        }

        template <class Types>
        bool OneOf(const Types& types, const unsigned char* type)
        {
            return std::any_of(types.begin(), types.end(), [&](const Tag& t) { return Is(type, t); });
        }

        struct ReadState
        {
            std::unordered_set<Key> placeable;   // every record of kPlaceableTypes read so far
            const ConfigWants* wants = nullptr;
            const std::unordered_map<Key, Key>* tomeSpells = nullptr;   // books the game pairs with a spell
            std::unordered_map<Key, std::string> named;   // records whose editor id a config names (lower case)
        };

        class Reader
        {
        public:
            Reader(SpellStore& store, ReadState& state, const RecordView& record, const Resolver& resolve) :
                m_store(store), m_state(state), m_placeable(state.placeable), m_record(record), m_resolve(resolve),
                m_key(resolve(record.formId))
            {}

            bool Read()
            {
                const unsigned char* t = m_record.type;
                if (Is(t, T("REFR")) || Is(t, T("ACHR"))) return ReadRef();
                if (Is(t, T("CELL"))) return ReadCell();
                if (Is(t, T("LCTN"))) return ReadLocation();
                if (Is(t, T("WRLD"))) return ReadWorldspace();
                return ReadBase(t);
            }

        private:
            SpellStore& m_store;
            ReadState& m_state;
            std::unordered_set<Key>& m_placeable;
            const RecordView& m_record;
            const Resolver& m_resolve;
            Key m_key;

            Key Form(const Subrecord& sub, std::size_t offset = 0) const
            {
                if (sub.size < offset + kFormSize) return 0;
                const std::uint32_t id = U32(sub.data + offset);
                return id ? m_resolve(id) : 0;
            }

            template <class Fn>
            bool Each(Fn&& fn) const
            {
                return ForEachSubrecord(m_record.body, m_record.size, std::forward<Fn>(fn));
            }

            // A record the model reads as a whole: the winning version replaces the earlier one
            void Forget()
            {
                m_store.kinds.erase(m_key);
                m_store.bookSpells.erase(m_key);
                m_store.lists.erase(m_key);
                m_store.inventories.erase(m_key);
                m_store.npcs.erase(m_key);
                m_store.vendorChests.erase(m_key);
                m_store.recipes.erase(m_key);
                m_store.questItems.erase(m_key);
                m_store.formLists.erase(m_key);
                m_store.holders.erase(m_key);
                m_store.raceKeywords.erase(m_key);
                m_store.keywordIds.erase(m_key);
            }

            void Forms(const Subrecord& s, std::vector<Key>& out) const
            {
                for (std::size_t i = 0; i + kFormSize <= s.size; i += kFormSize) {
                    if (const Key e = Form(s, i)) out.push_back(e);
                }
            }

            // What every record type gives: the configs' editor ids and types, and scripts
            bool ReadCommon(const unsigned char* t)
            {
                bool first = true;
                bool firstIsEditorId = false;
                bool editorIdSeen = false;
                std::string editorId;
                std::optional<Subrecord> vmad;
                const bool holder = OneOf(kHolderTypes, t);
                const bool whole = Each([&](const Subrecord& s) {
                    if (Is(s.type, T("EDID")) && !editorIdSeen) {
                        editorIdSeen = true;
                        editorId = ZString(s);
                        firstIsEditorId = first;
                    } else if (holder && !vmad && Is(s.type, T("VMAD")) && s.size) {
                        vmad = s;
                    }
                    first = false;
                });
                if (const ConfigWants* wants = m_state.wants) {
                    // The Python indexes editor ids written first in the record
                    auto lower = AsciiText::ToLower(editorId);
                    const bool named = firstIsEditorId && wants->editorIds.contains(lower);
                    if (named) m_state.named[m_key] = std::move(lower);
                    else m_state.named.erase(m_key);
                    if (named || wants->keys.contains(m_key)) std::memcpy(m_store.tokenTypes[m_key].data(), t, 4);
                }
                if (vmad) {
                    auto scripts = ParseVmad(vmad->data, vmad->size, m_resolve);
                    if (!scripts.empty()) {
                        ScriptHolder& h = m_store.holders[m_key];
                        std::memcpy(h.type.data(), t, 4);
                        h.editorId = editorId;
                        h.scripts = std::move(scripts);
                    }
                }
                return whole;
            }

            bool ReadBase(const unsigned char* t)
            {
                Forget();
                if (PlaceableType(t)) m_placeable.insert(m_key);
                const bool common = ReadCommon(t);
                std::string editorId;
                std::optional<Kind> kind;
                bool whole = true;
                if (Is(t, T("BOOK"))) {
                    Key spell = 0;
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("DATA")) && s.size >= kBookSpellOffset + kFormSize && (s.data[0] & kTeachesSpell))
                            spell = Form(s, kBookSpellOffset);
                    });
                    // A book that teaches nothing itself (Spell Research Synthesizer's): its pair's spell
                    if (!spell && m_state.tomeSpells) {
                        if (const auto pair = m_state.tomeSpells->find(m_key); pair != m_state.tomeSpells->end()) spell = pair->second;
                    }
                    if (spell) {
                        kind = Kind::kBook;
                        m_store.bookSpells[m_key] = spell;
                    }
                } else if (Is(t, T("SCRL"))) {
                    kind = Kind::kScroll;
                    whole = Each([&](const Subrecord& s) { if (Is(s.type, T("EDID"))) editorId = ZString(s); });
                } else if (Is(t, T("WEAP"))) {
                    bool staff = false;
                    bool enchanted = false;
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("DNAM")) && s.size >= 1) staff = s.data[0] == kStaffAnimation;
                        else if (Is(s.type, T("EITM"))) enchanted = Form(s) != 0;
                    });
                    if (staff && enchanted) kind = Kind::kStaff;
                } else if (Is(t, T("LVLI")) || Is(t, T("LVLN"))) {
                    kind = Is(t, T("LVLI")) ? Kind::kItemList : Kind::kNpcList;
                    auto& entries = m_store.lists[m_key];
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("LVLO"))) {
                            if (const Key e = Form(s, kLevelEntryFormOffset)) entries.push_back(e);
                        }
                    });
                } else if (Is(t, T("CONT")) || Is(t, T("OTFT"))) {
                    const bool outfit = Is(t, T("OTFT"));
                    kind = outfit ? Kind::kOutfit : Kind::kContainer;
                    auto& items = m_store.inventories[m_key];
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (!outfit && Is(s.type, T("CNTO"))) {
                            if (const Key e = Form(s)) items.push_back(e);
                        } else if (outfit && Is(s.type, T("INAM"))) {
                            for (std::size_t i = 0; i + kFormSize <= s.size; i += kFormSize) {
                                if (const Key e = Form(s, i)) items.push_back(e);
                            }
                        }
                    });
                } else if (Is(t, T("NPC_"))) {
                    kind = Kind::kNpc;
                    Npc npc;
                    bool fullSeen = false;
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("CNTO"))) {
                            if (const Key e = Form(s)) npc.items.push_back(e);
                        } else if (Is(s.type, T("INAM"))) npc.deathItem = Form(s);
                        else if (Is(s.type, T("DOFT"))) npc.outfit = Form(s);
                        else if (Is(s.type, T("TPLT"))) npc.templ = Form(s);
                        else if (Is(s.type, T("SNAM"))) {
                            if (const Key f = Form(s)) npc.factions.push_back(f);
                        } else if (Is(s.type, T("ACBS")) && s.size >= kFormSize) npc.flags = U32(s.data);
                        else if (Is(s.type, T("KWDA"))) Forms(s, npc.keywords);
                        else if (Is(s.type, T("RNAM")) && !npc.race) npc.race = Form(s);
                        else if (Is(s.type, T("FULL")) && !fullSeen) {
                            fullSeen = true;
                            ReadName(s, npc);
                        }
                    });
                    m_store.npcs[m_key] = std::move(npc);
                } else if (Is(t, T("FLST"))) {
                    kind = Kind::kFormList;
                    auto& members = m_store.formLists[m_key];
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("LNAM"))) Forms(s, members);
                    });
                } else if (Is(t, T("RACE"))) {
                    auto& keywords = m_store.raceKeywords[m_key];
                    whole = Each([&](const Subrecord& s) { if (Is(s.type, T("KWDA"))) Forms(s, keywords); });
                } else if (Is(t, T("KYWD"))) {
                    whole = Each([&](const Subrecord& s) { if (Is(s.type, T("EDID"))) m_store.keywordIds[m_key] = ZString(s); });
                } else if (Is(t, T("FACT"))) {
                    kind = Kind::kFaction;
                    Key chest = 0;
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("VENC"))) chest = Form(s);
                    });
                    if (chest) m_store.vendorChests[m_key] = chest;
                } else if (Is(t, T("COBJ"))) {
                    kind = Kind::kRecipe;
                    Recipe recipe;
                    recipe.plugin = m_resolve.self;
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("CNAM"))) recipe.created = Form(s);
                        else if (Is(s.type, T("BNAM"))) recipe.bench = Form(s);
                        else if (Is(s.type, T("CNTO"))) {
                            if (const Key e = Form(s)) recipe.components.push_back(e);
                        } else if (Is(s.type, T("CTDA"))) recipe.conditions.emplace_back(s.data, s.data + s.size);
                    });
                    if (recipe.created) m_store.recipes[m_key] = std::move(recipe);
                } else if (Is(t, T("QUST"))) {
                    kind = Kind::kQuest;
                    auto& items = m_store.questItems[m_key];
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("ALCO")) || Is(s.type, T("CNTO"))) {   // a QUST has CNTO only in aliases
                            if (const Key e = Form(s)) items.push_back(e);
                        }
                    });
                }
                if (kind) {
                    m_store.kinds[m_key] = *kind;
                    m_store.editorIds[m_key] = std::move(editorId);
                } else {
                    m_store.editorIds.erase(m_key);
                }
                return whole && common;
            }

            // FULL: a string table id in a localized plugin, else the text (kept when a config names it)
            void ReadName(const Subrecord& s, Npc& npc) const
            {
                if (m_resolve.localized && s.size == kFormSize) {
                    npc.nameId = U32(s.data);
                    npc.namePlugin = m_resolve.self;
                    return;
                }
                const ConfigWants* wants = m_state.wants;
                std::string name = ZString(s);
                if (wants && wants->strings.contains(AsciiText::ToLower(name))) npc.name = std::move(name);
            }

            bool ReadRef()
            {
                Key base = 0;
                const bool whole = Each([&](const Subrecord& s) {
                    if (Is(s.type, T("NAME"))) base = Form(s);
                });
                // Kept by its base's record type; ReadSpellStore checks the winning kind
                if (!m_record.cell || !m_placeable.contains(base)) {
                    m_store.refs.erase(m_key);
                    return whole;
                }
                m_store.refs[m_key] = PlacedRef{ base, m_resolve(m_record.cell), m_record.flags };
                return whole;
            }

            bool ReadCell()
            {
                Key location = 0;
                std::string editorId;
                bool interior = false;
                const bool whole = Each([&](const Subrecord& s) {
                    if (Is(s.type, T("EDID"))) editorId = ZString(s);
                    else if (Is(s.type, T("XLCN"))) location = Form(s);
                    else if (Is(s.type, T("DATA")) && s.size >= 1) interior = (s.data[0] & kInteriorCell) != 0;
                });
                // Field by field: a later version without a location or worldspace keeps the earlier one's
                Cell& cell = m_store.cells[m_key];
                if (m_record.worldspace && !interior) cell.worldspace = m_resolve(m_record.worldspace);
                if (location) cell.location = location;
                if (!editorId.empty()) cell.editorId = std::move(editorId);
                cell.interior = interior;
                return whole;
            }

            bool ReadLocation()
            {
                Location location;
                const bool whole = Each([&](const Subrecord& s) {
                    if (Is(s.type, T("EDID"))) location.editorId = ZString(s);
                    else if (Is(s.type, T("PNAM"))) location.parent = Form(s);
                });
                m_store.locations[m_key] = std::move(location);
                return whole;
            }

            bool ReadWorldspace()
            {
                std::string editorId;
                const bool whole = Each([&](const Subrecord& s) { if (Is(s.type, T("EDID"))) editorId = ZString(s); });
                m_store.worldspaces[m_key] = std::move(editorId);
                return whole;
            }
        };
    }

    SpellStore ReadSpellStore(const std::vector<PluginFile>& loadOrder, NameTable& names, const ConfigWants* wants,
        const std::unordered_map<Key, Key>* tomeSpells)
    {
        SpellStore store;
        ReadState state;
        state.wants = wants;
        state.tomeSpells = tomeSpells;
        for (const auto& plugin : loadOrder) {
            const auto scan = WalkPlugin(plugin, names, Wanted, [&](const RecordView& record, const Resolver& resolve) {
                return Reader(store, state, record, resolve).Read();
            });
            if (scan.status == ScanStatus::kUnreadable) store.unreadable.push_back(plugin.name);
            else if (scan.status == ScanStatus::kDamaged) store.damaged.push_back(plugin.name);
        }
        // Only references of the placed kinds stay, by their bases' winning versions
        std::erase_if(store.refs, [&](const auto& ref) {
            const auto kind = store.kinds.find(ref.second.base);
            return kind == store.kinds.end() || !PlacedKind(kind->second);
        });
        store.refs.rehash(0);

        // An editor id the configs name: the record that comes first (its own plugin's load
        // order, then the plugin's name and the form id), as the Python's by_edid
        std::unordered_map<std::uint32_t, std::size_t> position;
        for (std::size_t i = 0; i < loadOrder.size(); ++i) position.try_emplace(names.Id(loadOrder[i].name), i);
        const auto order = [&](Key k) {
            const auto p = position.find(PluginOf(k));
            return std::make_tuple(p != position.end() ? p->second : loadOrder.size(), names.Name(PluginOf(k)), LocalOf(k));
        };
        for (const auto& [key, editorId] : state.named) {
            auto [it, added] = store.tokenEditorIds.try_emplace(editorId, key);
            if (!added && order(key) < order(it->second)) it->second = key;
        }
        return store;
    }
}
