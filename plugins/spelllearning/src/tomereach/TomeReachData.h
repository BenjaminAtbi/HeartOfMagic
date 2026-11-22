#pragma once

// =============================================================================
// TOME REACH - DATA FILES (distribution model)
// =============================================================================
//
// The files the model reads besides plugins, as the game would see them: SPID
// (*_DISTR.ini at the top of Data), SkyPatcher-syntax (SKSE/Plugins/SkyPatcher/**.ini and
// Paths of Empyrean's SKSE/Plugins/POE/**.ini) and Container Distribution Framework
// (SKSE/Plugins/ContainerDistributionFramework/*.json)
// configs, compiled scripts (Scripts/*.pex) and string tables (Strings/*). A loose
// file wins; otherwise the archive the game registered last that holds it. Data
// folders are given lowest priority first (the game's Data folder alone in game;
// game Data, mods and overwrite in tools/tome-reach-test): a later folder's file,
// loose or archive, replaces an earlier one's of the same name. Archives are read in
// the game's order (ArchiveOrder), not by name. Pure C++, shared by the plugin and
// tools/tome-reach-test (scripts/bsaorder.py in the author's workspace is the same rule).

#include "tomereach/TomeReachArchive.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TomeReach
{
    struct ConfigFile
    {
        std::string path;                    // data path, lower case, '/' separated
        std::filesystem::path file;          // the winning copy
    };

    // The archives the game registers, lowest priority first (a later one wins), lower case:
    // the INI lists ([Archive] sResourceArchiveList, then sResourceArchiveList2), then for each
    // plugin in load order <stem>.bsa and <stem> - Textures.bsa; each name once, at its first
    // place. An archive not named here is never read (its plugin is not loaded).
    std::vector<std::string> ArchiveOrder(const std::vector<std::string>& iniArchives, const std::vector<std::string>& plugins);

    // An INI archive list ("A.bsa, B.bsa"): its names, trimmed, empty ones left out
    std::vector<std::string> SplitArchiveList(std::string_view list);

    class DataFiles
    {
    public:
        // `archives`: ArchiveOrder's names; the Data folders' archives they do not name are not read
        DataFiles(const std::vector<std::filesystem::path>& dataDirs, const std::vector<std::string>& archives);

        [[nodiscard]] std::vector<ConfigFile> SpidConfigs() const;
        [[nodiscard]] std::vector<ConfigFile> SkyPatcherConfigs() const;
        [[nodiscard]] std::vector<ConfigFile> CdfConfigs() const;    // by path, as CDF reads them

        // A data file (lower case, '/' separated): the winning loose copy, else the
        // highest-priority archive member. False when neither exists or it cannot be read.
        bool Read(std::string_view path, std::vector<unsigned char>& out) const;

        [[nodiscard]] std::size_t ArchiveCount() const { return m_archives.size(); }

        // The archives read, lowest first (only those holding Scripts or Strings: index < ArchiveCount),
        // and the archive each packed member is read from, sorted: the harness's --archives dump
        [[nodiscard]] const std::filesystem::path& ArchiveFile(std::size_t index) const { return m_archives[index].Path(); }
        [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> PackedMembers() const;

    private:
        struct Packed
        {
            std::size_t archive;
            ArchiveMember member;
        };

        void Scan(const std::filesystem::path& dir, std::unordered_map<std::string, std::filesystem::path>& archives);
        void AddFolder(const std::filesystem::path& dir, std::string_view folder, bool recursive);

        std::unordered_map<std::string, std::filesystem::path> m_loose;
        std::vector<Archive> m_archives;
        std::unordered_map<std::string, Packed> m_packed;
    };

    // The whole of a text file (config files are small); empty when it cannot be read
    std::string ReadText(const std::filesystem::path& file);
}
