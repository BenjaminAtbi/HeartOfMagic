#include "tomereach/TomeReachModel.h"

#include <chrono>

namespace TomeReach
{
    namespace
    {
        long long Ms(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
        {
            return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count());
        }
    }

    Model::Model(const std::vector<PluginFile>& loadOrder, NameTable& names, const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& iniArchives, const std::unordered_map<Key, Key>* tomeSpells) :
        m_names(names)
    {
        const auto started = std::chrono::steady_clock::now();
        ConfigWants wants;
        if (!dataDirs.empty()) {
            std::vector<std::string> plugins;
            plugins.reserve(loadOrder.size());
            for (const auto& plugin : loadOrder) plugins.push_back(plugin.name);
            m_data.emplace(dataDirs, ArchiveOrder(iniArchives, plugins));
            m_configs = ReadDistributionConfigs(*m_data, names);
            wants = m_configs.Wants();
        }
        const auto configsRead = std::chrono::steady_clock::now();
        m_store = ReadSpellStore(loadOrder, names, m_data ? &wants : nullptr, tomeSpells);
        const auto storeRead = std::chrono::steady_clock::now();
        if (m_data) m_distribution = ResolveDistribution(m_configs, m_store, *m_data, names);
        m_graph = std::make_unique<ReachGraph>(m_store, Configs(), Data());
        m_sites = std::make_unique<Sites>(m_store, names, *m_graph);
        const auto built = std::chrono::steady_clock::now();
        m_times = { Ms(started, configsRead), Ms(configsRead, storeRead), Ms(storeRead, built) };
    }

    SpellSources Model::Classify()
    {
        Classifier classifier(m_store, *m_graph, *m_sites, Configs());
        return ClassifySpells(m_store, m_names, classifier);
    }
}
