#include "tomereach/TomeReachData.h"
#include "AsciiText.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_set>

namespace TomeReach
{
    namespace
    {
        constexpr std::string_view kSpidSuffix = "_distr.ini";
        constexpr std::string_view kIniSuffix = ".ini";
        constexpr std::string_view kArchiveSuffix = ".bsa";
        constexpr std::string_view kSkyPatcherFolder = "skse/plugins/skypatcher";
        // The loose folders and archive folders the model reads
        constexpr std::string_view kScripts = "scripts";
        constexpr std::string_view kStrings = "strings";

        std::string Lower(const std::filesystem::path& p)
        {
            const auto u8 = p.generic_u8string();
            std::string text(u8.begin(), u8.end());
            for (char& c : text) c = AsciiText::ToLower(c);
            return text;
        }

        bool ReadBytes(const std::filesystem::path& file, std::vector<unsigned char>& out)
        {
            std::ifstream in(file, std::ios::binary | std::ios::ate);
            if (!in) return false;
            out.resize(static_cast<std::size_t>(in.tellg()));
            in.seekg(0);
            return out.empty() || static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size())));
        }
    }

    std::string ReadText(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in) return {};
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::vector<std::string> SplitArchiveList(std::string_view list)
    {
        std::vector<std::string> out;
        while (!list.empty()) {
            const std::size_t comma = list.find(',');
            std::string_view name = list.substr(0, comma);
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '\r')) name.remove_suffix(1);
            if (!name.empty()) out.emplace_back(name);
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
        return out;
    }

    std::vector<std::string> ArchiveOrder(const std::vector<std::string>& iniArchives, const std::vector<std::string>& plugins)
    {
        std::vector<std::string> out;
        std::unordered_set<std::string> seen;
        const auto add = [&](std::string name) {
            name = AsciiText::ToLower(name);
            if (seen.insert(name).second) out.push_back(std::move(name));
        };
        for (const auto& name : iniArchives) add(name);
        for (const auto& plugin : plugins) {
            const std::size_t dot = plugin.rfind('.');
            const std::string stem = dot == std::string::npos ? plugin : plugin.substr(0, dot);
            add(stem + ".bsa");
            add(stem + " - Textures.bsa");
        }
        return out;
    }

    DataFiles::DataFiles(const std::vector<std::filesystem::path>& dataDirs, const std::vector<std::string>& archives)
    {
        std::unordered_map<std::string, std::filesystem::path> found;   // archive name -> the last folder's copy
        for (const auto& dir : dataDirs) Scan(dir, found);
        // In the game's order: a later archive's member replaces an earlier one's
        for (const auto& name : archives) {
            const auto file = found.find(name);
            if (file == found.end()) continue;
            Archive archive;
            const bool ok = archive.Open(file->second, [](std::string_view folder) { return folder == kScripts || folder == kStrings; });
            if (!ok || archive.Members().empty()) continue;
            const std::size_t index = m_archives.size();
            for (const auto& [member, entry] : archive.Members()) m_packed[member] = Packed{ index, entry };
            m_archives.push_back(std::move(archive));
        }
    }

    void DataFiles::Scan(const std::filesystem::path& dir, std::unordered_map<std::string, std::filesystem::path>& archives)
    {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string name = Lower(it->path().filename());
            if (name.ends_with(kSpidSuffix)) m_loose[name] = it->path();
            else if (name.ends_with(kArchiveSuffix)) archives[name] = it->path();
        }
        AddFolder(dir, kScripts, false);
        AddFolder(dir, kStrings, false);
        AddFolder(dir, kSkyPatcherFolder, true);
    }

    std::vector<std::pair<std::string, std::size_t>> DataFiles::PackedMembers() const
    {
        std::vector<std::pair<std::string, std::size_t>> out;
        out.reserve(m_packed.size());
        for (const auto& [member, packed] : m_packed) out.emplace_back(member, packed.archive);
        std::sort(out.begin(), out.end());
        return out;
    }

    void DataFiles::AddFolder(const std::filesystem::path& dir, std::string_view folder, bool recursive)
    {
        const std::filesystem::path root = dir / std::filesystem::path(std::u8string(folder.begin(), folder.end()));
        std::error_code ec;
        const auto add = [&](const std::filesystem::directory_entry& e) {
            if (!e.is_regular_file(ec)) return;
            // Lexical only: relative() canonicalises, and under MO2's virtual Data folder that resolves a mod's
            // file to its real folder, so the key would leave Data and the file would never be found
            m_loose[std::string(folder) + "/" + Lower(e.path().lexically_relative(root))] = e.path();
        };
        if (recursive) {
            for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) add(*it);
        } else {
            for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) add(*it);
        }
    }

    std::vector<ConfigFile> DataFiles::SpidConfigs() const
    {
        std::vector<ConfigFile> out;
        for (const auto& [path, file] : m_loose) {
            if (path.find('/') == std::string::npos && path.ends_with(kSpidSuffix)) out.push_back({ path, file });
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        return out;
    }

    std::vector<ConfigFile> DataFiles::SkyPatcherConfigs() const
    {
        std::vector<ConfigFile> out;
        for (const auto& [path, file] : m_loose) {
            if (path.starts_with(kSkyPatcherFolder) && path.size() > kSkyPatcherFolder.size() &&
                path[kSkyPatcherFolder.size()] == '/' && path.ends_with(kIniSuffix))
                out.push_back({ path, file });
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        return out;
    }

    bool DataFiles::Read(std::string_view path, std::vector<unsigned char>& out) const
    {
        const std::string key(path);
        if (const auto loose = m_loose.find(key); loose != m_loose.end()) return ReadBytes(loose->second, out);
        const auto packed = m_packed.find(key);
        return packed != m_packed.end() && m_archives[packed->second.archive].Read(packed->second.member, out);
    }
}
