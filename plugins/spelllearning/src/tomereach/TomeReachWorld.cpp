#include "tomereach/TomeReachWorld.h"
#include "tomereach/TomeReachPlugin.h"

#include <algorithm>
#include <optional>

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
            T("FACT"), T("COBJ"), T("QUST"), T("CELL"), T("LCTN"), T("REFR"), T("ACHR"),
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

        class Reader
        {
        public:
            Reader(World& world, const RecordView& record, const Resolver& resolve) :
                m_world(world), m_record(record), m_resolve(resolve), m_key(resolve(record.formId))
            {}

            bool Read()
            {
                const unsigned char* t = m_record.type;
                if (Is(t, T("REFR")) || Is(t, T("ACHR"))) return ReadRef();
                if (Is(t, T("CELL"))) return ReadCell();
                if (Is(t, T("LCTN"))) return ReadLocation();
                return ReadBase(t);
            }

        private:
            World& m_world;
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
                m_world.kinds.erase(m_key);
                m_world.bookSpells.erase(m_key);
                m_world.lists.erase(m_key);
                m_world.inventories.erase(m_key);
                m_world.npcs.erase(m_key);
                m_world.vendorChests.erase(m_key);
                m_world.recipes.erase(m_key);
                m_world.questItems.erase(m_key);
            }

            bool ReadBase(const unsigned char* t)
            {
                Forget();
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
                    if (spell) {
                        kind = Kind::kBook;
                        m_world.bookSpells[m_key] = spell;
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
                    auto& entries = m_world.lists[m_key];
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("LVLO"))) {
                            if (const Key e = Form(s, kLevelEntryFormOffset)) entries.push_back(e);
                        }
                    });
                } else if (Is(t, T("CONT")) || Is(t, T("OTFT"))) {
                    const bool outfit = Is(t, T("OTFT"));
                    kind = outfit ? Kind::kOutfit : Kind::kContainer;
                    auto& items = m_world.inventories[m_key];
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
                    });
                    m_world.npcs[m_key] = std::move(npc);
                } else if (Is(t, T("FACT"))) {
                    kind = Kind::kFaction;
                    Key chest = 0;
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("VENC"))) chest = Form(s);
                    });
                    if (chest) m_world.vendorChests[m_key] = chest;
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
                    if (recipe.created) m_world.recipes[m_key] = std::move(recipe);
                } else if (Is(t, T("QUST"))) {
                    kind = Kind::kQuest;
                    auto& items = m_world.questItems[m_key];
                    whole = Each([&](const Subrecord& s) {
                        if (Is(s.type, T("EDID"))) editorId = ZString(s);
                        else if (Is(s.type, T("ALCO")) || Is(s.type, T("CNTO"))) {   // a QUST has CNTO only in aliases
                            if (const Key e = Form(s)) items.push_back(e);
                        }
                    });
                }
                if (kind) {
                    m_world.kinds[m_key] = *kind;
                    m_world.editorIds[m_key] = std::move(editorId);
                } else {
                    m_world.editorIds.erase(m_key);
                }
                return whole;
            }

            bool ReadRef()
            {
                Key base = 0;
                const bool whole = Each([&](const Subrecord& s) {
                    if (Is(s.type, T("NAME"))) base = Form(s);
                });
                const auto kind = m_world.kinds.find(base);
                if (!m_record.cell || kind == m_world.kinds.end() || !PlacedKind(kind->second)) {
                    m_world.refs.erase(m_key);   // a later version moved it off the kept bases
                    return whole;
                }
                m_world.refs[m_key] = PlacedRef{ base, m_resolve(m_record.cell), m_record.flags };
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
                Cell& cell = m_world.cells[m_key];
                if (m_record.world && !interior) cell.world = m_resolve(m_record.world);
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
                m_world.locations[m_key] = std::move(location);
                return whole;
            }
        };
    }

    World ReadWorld(const std::vector<PluginFile>& loadOrder, NameTable& names)
    {
        World world;
        for (const auto& plugin : loadOrder) {
            const auto scan = WalkPlugin(plugin, names, Wanted, [&](const RecordView& record, const Resolver& resolve) {
                return Reader(world, record, resolve).Read();
            });
            if (scan.status == ScanStatus::kUnreadable) world.unreadable.push_back(plugin.name);
            else if (scan.status == ScanStatus::kDamaged) world.damaged.push_back(plugin.name);
        }
        return world;
    }
}
