#include "tomereach/TomeReachData.h"
#include "AsciiText.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

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

    DataFiles::DataFiles(const std::vector<std::filesystem::path>& dataDirs)
    {
        for (const auto& dir : dataDirs) Scan(dir);
    }

    void DataFiles::Scan(const std::filesystem::path& dir)
    {
        std::error_code ec;
        std::vector<std::filesystem::path> archives;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string name = Lower(it->path().filename());
            if (name.ends_with(kSpidSuffix)) m_loose[name] = it->path();
            else if (name.ends_with(kArchiveSuffix)) archives.push_back(it->path());
        }
        AddFolder(dir, kScripts, false);
        AddFolder(dir, kStrings, false);
        AddFolder(dir, kSkyPatcherFolder, true);

        // Archives: the later one wins, so a folder's own are taken in name order
        std::sort(archives.begin(), archives.end(), [](const auto& a, const auto& b) { return Lower(a) < Lower(b); });
        for (const auto& path : archives) {
            Archive archive;
            const bool ok = archive.Open(path, [](std::string_view folder) { return folder == kScripts || folder == kStrings; });
            if (!ok || archive.Members().empty()) continue;
            const std::size_t index = m_archives.size();
            for (const auto& [member, entry] : archive.Members()) m_packed[member] = Packed{ index, entry };
            m_archives.push_back(std::move(archive));
        }
    }

    void DataFiles::AddFolder(const std::filesystem::path& dir, std::string_view folder, bool recursive)
    {
        const std::filesystem::path root = dir / std::filesystem::path(std::u8string(folder.begin(), folder.end()));
        std::error_code ec;
        const auto add = [&](const std::filesystem::directory_entry& e) {
            if (!e.is_regular_file(ec)) return;
            m_loose[std::string(folder) + "/" + Lower(std::filesystem::relative(e.path(), root, ec))] = e.path();
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
