#pragma once

// =============================================================================
// TOME REACH - THE DISTRIBUTION MODEL, END TO END
// =============================================================================
//
// Reads the load order into the spell store, the data folders' SPID and
// SkyPatcher configs and scripts into the reach graph, and classifies every
// spell with a tome (TomeReachSegments.h). One object, as the plugin's worker
// thread (TomeReach.cpp) and tools/tome-reach-test use it. Pure C++.

#include "tomereach/TomeReachSegments.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace TomeReach
{
    struct ModelTimes
    {
        long long configs = 0;   // ms: data folders and configs read
        long long store = 0;     // the plugins read into the spell store
        long long graph = 0;     // configs resolved, scripts analysed, the reach graph built
    };

    class Model
    {
    public:
        // `dataDirs` empty: records only, no configs or scripts. `tomeSpells`: see ReadSpellStore.
        Model(const std::vector<PluginFile>& loadOrder, NameTable& names, const std::vector<std::filesystem::path>& dataDirs,
            const std::unordered_map<Key, Key>* tomeSpells = nullptr);
        Model(const Model&) = delete;
        Model& operator=(const Model&) = delete;

        [[nodiscard]] const SpellStore& Store() const { return m_store; }
        [[nodiscard]] const ReachGraph& Graph() const { return *m_graph; }
        [[nodiscard]] Sites& Places() { return *m_sites; }
        // The resolved SPID and SkyPatcher lines; null for records only
        [[nodiscard]] const Distribution* Configs() const { return m_data ? &m_distribution : nullptr; }
        [[nodiscard]] const DistributionConfigs& ConfigFiles() const { return m_configs; }
        [[nodiscard]] const DataFiles* Data() const { return m_data ? &*m_data : nullptr; }
        [[nodiscard]] const ModelTimes& Times() const { return m_times; }

        // Every spell of the store's tomes, with its routes and segment
        [[nodiscard]] SpellSources Classify();

    private:
        NameTable& m_names;
        std::optional<DataFiles> m_data;
        DistributionConfigs m_configs;
        SpellStore m_store;
        Distribution m_distribution;
        std::unique_ptr<ReachGraph> m_graph;
        std::unique_ptr<Sites> m_sites;
        ModelTimes m_times;
    };
}
