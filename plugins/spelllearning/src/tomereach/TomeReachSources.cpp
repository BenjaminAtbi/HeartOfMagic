#include "Common.h"
#include "librarian/LibrarianInternal.h"
#include "tomereach/TomeReachConfig.h"
#include "tomereach/TomeReachModel.h"
#include "tomereach/TomeReachSources.h"

#include <array>
#include <chrono>
#include <string>

namespace TomeReach
{
    SegmentSwitches ReadSegmentSwitches()
    {
        nlohmann::json config;
        namespace Detail = Librarian::Detail;
        if (!Detail::ReadJsonFile(Detail::DataPath("config.json"), config)) return {};
        return SegmentSwitchesOf(config);
    }

    nlohmann::json SourceJson(const SpellSourceInfo& info)
    {
        nlohmann::json out = { { "segment", std::string(SegmentKey(info.segment)) },
            { "class", std::string(ClassName(info.cls)) }, { "subtype", info.subtype }, { "detail", info.detail } };
        if (!info.shop.empty()) out["shop"] = info.shop;
        return out;
    }

    bool ClassifyForGame(const std::vector<PluginFile>& loadOrder, NameTable& names, const std::vector<std::string>& iniArchives,
        const std::unordered_map<Key, Key>& tomeSpells, const std::unordered_map<RE::FormID, Key>& spellKeys,
        std::unordered_map<RE::FormID, SpellSourceInfo>& out)
    {
        try {
            Model model(loadOrder, names, { std::filesystem::path(kDataDir) }, iniArchives, &tomeSpells);
            if (!model.Store().unreadable.empty()) {
                logger::warn("TomeReach: the distribution model could not read {} plugins (first: {}) - the reach pass "
                             "answers", model.Store().unreadable.size(), model.Store().unreadable.front());
                return false;
            }
            const auto started = std::chrono::steady_clock::now();
            const SpellSources sources = model.Classify();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            std::unordered_map<Key, RE::FormID> byKey;
            for (const auto& [formId, key] : spellKeys) {
                if (key) byKey.emplace(key, formId);
            }
            std::array<std::size_t, kSegmentCount> counts{};
            for (const auto& [key, s] : sources.spells) {
                const auto formId = byKey.find(key);
                if (formId == byKey.end()) continue;
                out[formId->second] = { s.segment, s.best.cls, s.best.subtype, s.best.detail, s.shop };
                ++counts[static_cast<std::size_t>(s.segment)];
            }
            std::string summary;
            for (std::size_t i = 0; i < kSegmentCount; ++i) {
                summary += std::format("{}{} {}", i ? ", " : "", SegmentName(static_cast<Segment>(i)), counts[i]);
            }
            const ModelTimes& t = model.Times();
            logger::info("TomeReach: distribution model: {} spells with a tome ({}), {} spellpack vendors; {} config "
                         "files, {} SPID lines, {} SkyPatcher adds, {} CDF changes ({} naming no container), {} archives with scripts or strings, {} scripts read, {} "
                         "not found; configs {} ms, plugins {} ms, graph {} ms, classes {} ms", out.size(), summary,
                sources.shops.size(), model.ConfigFiles().files, model.Configs()->spid.size(),
                model.Configs()->listAdds.size(), model.Configs()->cdf.size(), model.Graph().CdfUntraced(), model.Data() ? model.Data()->ArchiveCount() : 0,
                model.Graph().ScriptsRead(), model.Graph().ScriptsMissing(), t.configs, t.store, t.graph, ms);
            for (const std::string& w : model.Configs()->cdfWarnings) logger::warn("TomeReach: CDF: {}", w);
            for (const SpellpackShop& shop : sources.shops) {
                logger::info("TomeReach: spellpack vendor: {} - {} of {} vendor-sold tome spells", shop.plugin, shop.sold,
                    shop.set);
            }
            if (!model.Store().damaged.empty()) {
                logger::warn("TomeReach: the distribution model read {} plugins only in part (first: {})",
                    model.Store().damaged.size(), model.Store().damaged.front());
            }
            return true;
        } catch (const std::exception& e) {
            logger::warn("TomeReach: the distribution model failed ({}) - the reach pass answers", e.what());
        } catch (...) {
            logger::warn("TomeReach: the distribution model failed - the reach pass answers");
        }
        out.clear();
        return false;
    }
}
