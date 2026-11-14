#include "tomereach/TomeReachPlugin.h"

#include <zlib.h>

#include <fstream>

namespace TomeReach
{
    namespace
    {
        constexpr std::size_t kRecordHeader = 24;
        constexpr std::size_t kLabelOffset = 8;         // GRUP: label, then the group type
        constexpr std::size_t kGroupTypeOffset = 12;
        constexpr std::size_t kFlagsOffset = 8;         // a record: size, flags, form id
        constexpr std::size_t kFormIdOffset = 12;
        constexpr std::uint32_t kCompressed = 0x00040000;
        constexpr std::uint32_t kLightFile = 0x00000200;
        // No record inflates past this: a larger stated size means a damaged file
        constexpr uLongf kMaxInflatedRecord = 64u * 1024u * 1024u;
        constexpr std::size_t kInflatedSizeField = 4;   // a compressed record starts with its full size

        // Group types: 0 top (label: the record type), 1 a worldspace's children (label: the
        // worldspace), 6 a cell's children, 8 its persistent and 9 its temporary references
        // (label: the cell)
        constexpr std::uint32_t kTopGroup = 0;
        constexpr std::uint32_t kWorldChildren = 1;
        constexpr std::uint32_t kCellChildren = 6;
        constexpr std::uint32_t kCellPersistent = 8;
        constexpr std::uint32_t kCellTemporary = 9;

        bool ReadFile(const std::filesystem::path& path, std::vector<unsigned char>& out)
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in) return false;
            const auto size = static_cast<std::size_t>(in.tellg());
            out.resize(size);
            in.seekg(0);
            return size == 0 || static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()),
                                    static_cast<std::streamsize>(size)));
        }

        // Top groups that hold records of other types: cells and worldspaces hold their
        // references, dialogue its responses (INFO)
        bool HoldsChildren(const unsigned char* label)
        {
            return Is(label, T("CELL")) || Is(label, T("WRLD")) || Is(label, T("DIAL"));
        }

        struct Context
        {
            std::size_t end;
            std::uint32_t cell;
            std::uint32_t worldspace;
        };
    }

    PluginScan WalkPlugin(const PluginFile& file, NameTable& names, const RecordFilter& wants, const RecordReader& read)
    {
        PluginScan result;
        std::vector<unsigned char> data;
        if (!ReadFile(file.path, data) && (file.fallback.empty() || !ReadFile(file.fallback, data))) return result;
        if (data.size() < kRecordHeader || !Is(data.data(), T("TES4"))) return result;

        Resolver resolve{ names, {}, names.Id(file.name) };
        const std::uint32_t headerSize = U32(data.data() + 4);
        const std::uint32_t headerFlags = U32(data.data() + kFlagsOffset);
        if (kRecordHeader + headerSize > data.size()) return result;
        names.SetLight(resolve.self, (headerFlags & kLightFile) != 0);
        // A header read in part leaves the master list, and so every form id, in doubt
        const bool headerWhole = ForEachSubrecord(data.data() + kRecordHeader, headerSize, [&](const Subrecord& sub) {
            if (Is(sub.type, T("MAST"))) resolve.masters.push_back(names.Id(ZString(sub)));
        });
        if (!headerWhole) return result;

        const auto damaged = [&] {
            result.status = ScanStatus::kDamaged;
            result.scope = resolve.masters;
            result.scope.push_back(resolve.self);
            return result;
        };
        std::vector<Context> groups;
        std::vector<unsigned char> inflated;
        std::size_t pos = kRecordHeader + headerSize;
        while (pos + kRecordHeader <= data.size()) {
            while (!groups.empty() && groups.back().end <= pos) groups.pop_back();
            const unsigned char* header = data.data() + pos;
            const std::uint32_t size = U32(header + 4);
            if (Is(header, T("GRUP"))) {
                const unsigned char* label = header + kLabelOffset;
                const std::uint32_t type = U32(header + kGroupTypeOffset);
                if (size < kRecordHeader || pos + size > data.size()) return damaged();
                if (type == kTopGroup && !HoldsChildren(label) && !wants(label)) {
                    pos += size;  // a top group of a type nobody reads
                    continue;
                }
                Context context = groups.empty() ? Context{ 0, 0, 0 } : groups.back();
                context.end = pos + size;
                if (type == kWorldChildren) {
                    context.worldspace = U32(label);
                    context.cell = 0;
                } else if (type == kCellChildren || type == kCellPersistent || type == kCellTemporary) {
                    context.cell = U32(label);
                }
                groups.push_back(context);
                pos += kRecordHeader;  // step into the group; its records follow
                continue;
            }
            const std::uint32_t flags = U32(header + kFlagsOffset);
            const std::uint32_t formId = U32(header + kFormIdOffset);
            const unsigned char* body = header + kRecordHeader;
            pos += kRecordHeader + size;
            if (pos > data.size()) return damaged();  // a record runs past the end
            if (!wants(header)) continue;

            std::size_t bodySize = size;
            if (flags & kCompressed) {
                if (size < kInflatedSizeField) return damaged();
                uLongf length = U32(body);
                if (length > kMaxInflatedRecord) return damaged();
                inflated.resize(length);
                if (uncompress(inflated.data(), &length, body + kInflatedSizeField, size - kInflatedSizeField) != Z_OK) {
                    return damaged();
                }
                body = inflated.data();
                bodySize = length;
            }
            RecordView record{ header, formId, flags, body, bodySize };
            if (!groups.empty()) {
                record.cell = groups.back().cell;
                record.worldspace = groups.back().worldspace;
            }
            if (!read(record, resolve)) return damaged();
        }
        if (pos != data.size()) return damaged();  // the file ends inside a record header
        result.status = ScanStatus::kRead;
        return result;
    }
}
