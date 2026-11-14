#include "tomereach/TomeReachArchive.h"
#include "AsciiText.h"

#include <zlib.h>

#include <cstring>
#include <fstream>

namespace TomeReach
{
    namespace
    {
        constexpr std::uint32_t kLz4FrameMagic = 0x184D2204;
        constexpr std::uint8_t kLz4ContentSize = 0x08;      // FLG: an 8-byte content size follows
        constexpr std::uint8_t kLz4DictionaryId = 0x01;     // FLG: a 4-byte dictionary id follows
        constexpr std::uint8_t kLz4BlockChecksum = 0x10;    // FLG: every block ends with a 4-byte checksum
        constexpr std::size_t kLz4FrameHeader = 7;          // magic, FLG, BD, header checksum
        constexpr std::uint32_t kLz4RawBlock = 0x80000000;  // block size: stored, not compressed
        constexpr std::size_t kLz4MinMatch = 4;
        constexpr std::uint8_t kLz4MoreLength = 255;
        constexpr std::size_t kLz4LongLength = 15;

        constexpr std::size_t kHeaderSize = 36;
        constexpr std::uint32_t kSkyrimLe = 104;            // zlib
        constexpr std::uint32_t kSkyrimSe = 105;            // LZ4 frames
        constexpr std::size_t kFolderRecordLe = 16;         // hash, count, offset
        constexpr std::size_t kFolderRecordSe = 24;         // hash, count, pad, 64-bit offset
        constexpr std::size_t kFolderCountOffset = 8;
        constexpr std::size_t kFileRecord = 16;             // hash, size, offset
        constexpr std::uint32_t kHasFolderNames = 0x1;
        constexpr std::uint32_t kHasFileNames = 0x2;
        constexpr std::uint32_t kCompressedByDefault = 0x4;
        constexpr std::uint32_t kEmbeddedNames = 0x100;
        constexpr std::uint32_t kToggleCompression = 0x40000000;   // file size: the other way from the default
        constexpr std::uint32_t kSizeMask = 0x3FFFFFFF;
        constexpr std::size_t kOriginalSizeField = 4;       // a compressed file starts with its full size
        constexpr std::uint32_t kMaxInflated = 256u * 1024u * 1024u;

        std::uint32_t Le32(const unsigned char* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        bool Lz4Block(const unsigned char* src, std::size_t n, std::vector<unsigned char>& dst)
        {
            std::size_t i = 0;
            const auto length = [&](std::size_t& value) {
                if (value != kLz4LongLength) return true;
                std::uint8_t b = 0;
                do {
                    if (i >= n) return false;
                    b = src[i++];
                    value += b;
                } while (b == kLz4MoreLength);
                return true;
            };
            while (i < n) {
                const std::uint8_t token = src[i++];
                std::size_t literals = token >> 4;
                if (!length(literals) || literals > n - i) return false;
                dst.insert(dst.end(), src + i, src + i + literals);
                i += literals;
                if (i >= n) break;
                if (i + 2 > n) return false;
                const std::size_t offset = src[i] | (static_cast<std::size_t>(src[i + 1]) << 8);
                i += 2;
                std::size_t match = token & 0x0F;
                if (!length(match)) return false;
                match += kLz4MinMatch;
                if (offset == 0 || offset > dst.size() || dst.size() + match > kMaxInflated) return false;
                const std::size_t start = dst.size() - offset;
                for (std::size_t k = 0; k < match; ++k) dst.push_back(dst[start + k]);
            }
            return true;
        }

        bool ReadAt(std::ifstream& in, std::uint64_t offset, std::size_t size, std::vector<unsigned char>& out)
        {
            out.resize(size);
            in.seekg(static_cast<std::streamoff>(offset));
            return size == 0 || static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)));
        }
    }

    bool InflateLz4Frame(const unsigned char* data, std::size_t size, std::vector<unsigned char>& out)
    {
        if (size < kLz4FrameHeader || Le32(data) != kLz4FrameMagic) return false;
        const std::uint8_t flags = data[4];
        std::size_t i = kLz4FrameHeader + ((flags & kLz4ContentSize) ? 8 : 0) + ((flags & kLz4DictionaryId) ? 4 : 0);
        while (i + 4 <= size) {
            std::uint32_t block = Le32(data + i);
            i += 4;
            if (block == 0) break;
            const bool raw = (block & kLz4RawBlock) != 0;
            block &= ~kLz4RawBlock;
            if (block > size - i) return false;
            if (raw) out.insert(out.end(), data + i, data + i + block);
            else if (!Lz4Block(data + i, block, out)) return false;
            i += block;
            if (flags & kLz4BlockChecksum) i += 4;
        }
        return true;
    }

    bool Archive::Open(const std::filesystem::path& path, const std::function<bool(std::string_view)>& wantFolder)
    {
        m_path = path;
        m_members.clear();
        std::ifstream in(path, std::ios::binary);
        std::vector<unsigned char> header;
        if (!in || !ReadAt(in, 0, kHeaderSize, header) || std::memcmp(header.data(), "BSA\0", 4) != 0) return false;
        m_version = Le32(header.data() + 4);
        if (m_version != kSkyrimLe && m_version != kSkyrimSe) return false;
        const std::uint32_t folderOffset = Le32(header.data() + 8);
        m_flags = Le32(header.data() + 12);
        const std::uint32_t folders = Le32(header.data() + 16);
        const std::uint32_t files = Le32(header.data() + 20);
        const std::uint32_t folderNames = Le32(header.data() + 24);
        const std::uint32_t fileNames = Le32(header.data() + 28);

        // Folder records, then each folder's name and file records, then the file names
        const std::size_t record = m_version == kSkyrimSe ? kFolderRecordSe : kFolderRecordLe;
        const std::size_t blocks = (m_flags & kHasFolderNames ? folders + static_cast<std::size_t>(folderNames) : 0) +
                                   static_cast<std::size_t>(files) * kFileRecord;
        std::vector<unsigned char> dir;
        if (!ReadAt(in, folderOffset, folders * record + blocks, dir)) return false;

        struct Wanted { std::string folder; std::size_t index; ArchiveMember member; };
        std::vector<Wanted> wanted;
        std::size_t o = folders * record;
        std::size_t index = 0;
        for (std::uint32_t f = 0; f < folders; ++f) {
            const std::uint32_t count = Le32(dir.data() + f * record + kFolderCountOffset);
            std::string folder;
            if (m_flags & kHasFolderNames) {
                if (o >= dir.size()) return false;
                const std::size_t length = dir[o++];
                if (length > dir.size() - o) return false;
                folder.assign(reinterpret_cast<const char*>(dir.data() + o), strnlen(reinterpret_cast<const char*>(dir.data() + o), length));
                o += length;
                for (char& c : folder) c = c == '\\' ? '/' : AsciiText::ToLower(c);
            }
            if (static_cast<std::size_t>(count) * kFileRecord > dir.size() - o) return false;
            if (wantFolder(folder)) {
                for (std::uint32_t k = 0; k < count; ++k) {
                    const unsigned char* r = dir.data() + o + k * kFileRecord;
                    const std::uint32_t stated = Le32(r + 8);
                    const bool compressed = ((m_flags & kCompressedByDefault) != 0) != ((stated & kToggleCompression) != 0);
                    wanted.push_back({ folder, index + k, ArchiveMember{ stated & kSizeMask, Le32(r + 12), compressed } });
                }
            }
            o += count * kFileRecord;
            index += count;
        }
        if (wanted.empty() || !(m_flags & kHasFileNames)) return true;

        std::vector<unsigned char> names;
        if (!ReadAt(in, folderOffset + o, fileNames, names)) return false;
        std::vector<std::size_t> starts{ 0 };
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == 0) starts.push_back(i + 1);
        }
        for (auto& w : wanted) {
            if (w.index + 1 >= starts.size()) continue;
            std::string name(reinterpret_cast<const char*>(names.data() + starts[w.index]), starts[w.index + 1] - starts[w.index] - 1);
            for (char& c : name) c = c == '\\' ? '/' : AsciiText::ToLower(c);
            m_members[w.folder + "/" + name] = w.member;
        }
        return true;
    }

    bool Archive::Read(const ArchiveMember& member, std::vector<unsigned char>& out) const
    {
        out.clear();
        std::ifstream in(m_path, std::ios::binary);
        std::vector<unsigned char> blob;
        if (!in || !ReadAt(in, member.offset, member.size, blob)) return false;
        std::size_t start = 0;
        if (m_flags & kEmbeddedNames) {
            if (blob.empty()) return false;
            start = 1 + static_cast<std::size_t>(blob[0]);
        }
        if (start > blob.size()) return false;
        if (!member.compressed) {
            out.assign(blob.begin() + static_cast<std::ptrdiff_t>(start), blob.end());
            return true;
        }
        if (blob.size() - start < kOriginalSizeField) return false;
        const std::uint32_t original = Le32(blob.data() + start);
        if (original > kMaxInflated) return false;
        const unsigned char* packed = blob.data() + start + kOriginalSizeField;
        const std::size_t packedSize = blob.size() - start - kOriginalSizeField;
        if (m_version == kSkyrimSe) {
            out.reserve(original);
            return InflateLz4Frame(packed, packedSize, out);
        }
        out.resize(original);
        uLongf length = original;
        if (uncompress(out.data(), &length, packed, static_cast<uLong>(packedSize)) != Z_OK) return false;
        out.resize(length);
        return true;
    }
}
