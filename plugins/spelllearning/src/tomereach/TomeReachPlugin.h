#pragma once

// =============================================================================
// TOME REACH - ONE PLUGIN FILE, RECORD BY RECORD
// =============================================================================
//
// Reads a plugin file: its header and master list, then every record of the
// types asked for, compressed ones inflated, with the cell and worldspace whose
// group holds it (a placed reference's cell, a cell's worldspace). Top groups
// of other types are skipped unread. The walker under the reach pass
// (TomeReachRecords.cpp) and the spell store (TomeReachReader.cpp). Pure C++.

#include "tomereach/TomeReachRecords.h"

#include <array>
#include <cstring>
#include <functional>

namespace TomeReach
{
    using Tag = std::array<char, 4>;

    constexpr Tag T(const char (&s)[5]) { return { s[0], s[1], s[2], s[3] }; }

    inline bool Is(const unsigned char* p, const Tag& tag) { return std::memcmp(p, tag.data(), 4) == 0; }

    inline std::uint32_t U32(const unsigned char* p)
    {
        std::uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    inline std::uint16_t U16(const unsigned char* p)
    {
        std::uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    inline constexpr std::size_t kSubHeader = 6;
    inline constexpr std::size_t kBigSizeField = 4;   // XXXX: the next subrecord's real size

    struct Subrecord
    {
        const unsigned char* type;
        const unsigned char* data;
        std::size_t size;
    };

    // Calls fn for each subrecord; false when the last one runs past `size`
    template <class Fn>
    bool ForEachSubrecord(const unsigned char* data, std::size_t size, Fn&& fn)
    {
        std::size_t i = 0;
        std::size_t bigSize = 0;
        while (i + kSubHeader <= size) {
            const unsigned char* type = data + i;
            std::size_t length = U16(data + i + 4);
            i += kSubHeader;
            if (Is(type, T("XXXX")) && length >= kBigSizeField && i + kBigSizeField <= size) {
                bigSize = U32(data + i);
                i += length;
                continue;
            }
            if (bigSize) {
                length = bigSize;
                bigSize = 0;
            }
            if (i + length > size) return false;
            fn(Subrecord{ type, data + i, length });
            i += length;
        }
        return i == size;
    }

    // The text of a zero-terminated string subrecord (EDID)
    inline std::string ZString(const Subrecord& sub)
    {
        const auto* text = reinterpret_cast<const char*>(sub.data);
        return std::string(text, strnlen(text, sub.size));
    }

    // A plugin's view of form ids: the high byte picks a master, or the plugin itself
    struct Resolver
    {
        NameTable& names;
        std::vector<std::uint32_t> masters;
        std::uint32_t self = 0;
        bool localized = false;   // names (FULL) are ids into the plugin's string tables

        Key operator()(std::uint32_t formId) const
        {
            const std::uint32_t index = formId >> kFileIndexShift;
            const std::uint32_t plugin = index < masters.size() ? masters[index] : self;
            const std::uint32_t mask = names.IsLight(plugin) ? kLightLocalMask : kLocalMask;
            return MakeKey(plugin, formId & mask);
        }
    };

    inline constexpr std::uint32_t kNoGroup = 0xFFFFFFFF;
    inline constexpr std::uint32_t kWorldChildren = 1;   // group type: a worldspace's children

    struct RecordView
    {
        const unsigned char* type;     // four bytes
        std::uint32_t formId = 0;      // as the plugin writes it (resolve it)
        std::uint32_t flags = 0;       // record header flags
        const unsigned char* body = nullptr;   // inflated when compressed
        std::size_t size = 0;
        std::uint32_t cell = 0;        // the cell whose children group holds it, as written; 0: none
        std::uint32_t worldspace = 0;      // the worldspace whose group holds it, as written; 0: none
        std::uint32_t group = kNoGroup;    // the type of the group right around it (kWorldChildren: a world's
                                           // persistent cell)
    };

    inline constexpr std::uint32_t kDeletedRecord = 0x00000020;
    inline constexpr std::uint32_t kDisabledRecord = 0x00000800;   // a placed reference "initially disabled"

    // `wants(type)`: the record types to read. `read(record, resolve)`: false when the
    // record was read only in part (the plugin is then damaged, see PluginScan).
    // `resolve` is the plugin's own (masters, self) for the whole walk.
    using RecordFilter = std::function<bool(const unsigned char* type)>;
    using RecordReader = std::function<bool(const RecordView& record, const Resolver& resolve)>;

    PluginScan WalkPlugin(const PluginFile& file, NameTable& names, const RecordFilter& wants, const RecordReader& read);
}
