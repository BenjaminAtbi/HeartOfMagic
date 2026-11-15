#pragma once

// =============================================================================
// TOME REACH - BETHESDA ARCHIVES (distribution model)
// =============================================================================
//
// Reads files out of Skyrim's BSA archives: version 104 (zlib) and 105 (LZ4
// frames), the members of chosen folders only (compiled scripts, string tables).
// Port of the author's Python reader (scripts/modindex/lib/binfmt.py bsa_read and
// lz4_frame), with no library beyond zlib. Pure C++, shared by the plugin and
// tools/tome-reach-test.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace TomeReach
{
    // Decodes an LZ4 frame (as SSE archives store compressed files); false when it is not one or is damaged
    bool InflateLz4Frame(const unsigned char* data, std::size_t size, std::vector<unsigned char>& out);

    struct ArchiveMember
    {
        std::uint32_t size = 0;      // as stored (compressed, embedded name included)
        std::uint32_t offset = 0;
        bool compressed = false;
    };

    class Archive
    {
    public:
        // Reads the directory, keeping the members of the folders `wantFolder` accepts (lower
        // case, '/' separated). False when the file is not a version 104 or 105 archive.
        bool Open(const std::filesystem::path& path, const std::function<bool(std::string_view)>& wantFolder);

        // Members kept by Open, by "folder/file" (lower case, '/' separated)
        [[nodiscard]] const std::unordered_map<std::string, ArchiveMember>& Members() const { return m_members; }

        // A member's contents, inflated
        bool Read(const ArchiveMember& member, std::vector<unsigned char>& out) const;

        [[nodiscard]] const std::filesystem::path& Path() const { return m_path; }

    private:
        std::filesystem::path m_path;
        std::uint32_t m_version = 0;
        std::uint32_t m_flags = 0;
        std::unordered_map<std::string, ArchiveMember> m_members;
    };
}
