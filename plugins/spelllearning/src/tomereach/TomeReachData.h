#pragma once

// =============================================================================
// TOME REACH - DATA FILES (distribution model)
// =============================================================================
//
// The files the model reads besides plugins, as the game would see them: SPID
// (*_DISTR.ini at the top of Data) and SkyPatcher (SKSE/Plugins/SkyPatcher/**.ini)
// configs, compiled scripts (Scripts/*.pex) and string tables (Strings/*). A loose
// file wins; otherwise the highest-priority archive that holds it. Data folders are
// given lowest priority first (the game's Data folder alone in game; game Data,
// mods and overwrite in tools/tome-reach-test), archives within one folder by name.
// Pure C++, shared by the plugin and tools/tome-reach-test.

#include "tomereach/TomeReachArchive.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace TomeReach
{
    struct ConfigFile
    {
        std::string path;                    // data path, lower case, '/' separated
        std::filesystem::path file;          // the winning copy
    };

    class DataFiles
    {
    public:
        explicit DataFiles(const std::vector<std::filesystem::path>& dataDirs);

        [[nodiscard]] std::vector<ConfigFile> SpidConfigs() const;
        [[nodiscard]] std::vector<ConfigFile> SkyPatcherConfigs() const;

        // A data file (lower case, '/' separated): the winning loose copy, else the
        // highest-priority archive member. False when neither exists or it cannot be read.
        bool Read(std::string_view path, std::vector<unsigned char>& out) const;

        [[nodiscard]] std::size_t ArchiveCount() const { return m_archives.size(); }

    private:
        struct Packed
        {
            std::size_t archive;
            ArchiveMember member;
        };

        void Scan(const std::filesystem::path& dir);
        void AddFolder(const std::filesystem::path& dir, std::string_view folder, bool recursive);

        std::unordered_map<std::string, std::filesystem::path> m_loose;
        std::vector<Archive> m_archives;
        std::unordered_map<std::string, Packed> m_packed;
    };

    // The whole of a text file (config files are small); empty when it cannot be read
    std::string ReadText(const std::filesystem::path& file);
}
