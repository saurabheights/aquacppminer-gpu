#include "args.h"

#include <assert.h>

#include <iostream>
#include <set>

#include "http.h"
#include "inputParser.h"
#include "log.h"
#include "miner.h"
#include "miningConfig.h"
#include "string_utils.h"

void printUsage() {
    printf("\n%s\n", s_usageMsg.c_str());
}

std::pair<bool, uint32_t> parseRefreshRate(const std::string& refreshRateStr) {
    char unit[8];
    float refreshRate;
#ifdef _MSC_VER
    int count = sscanf_s(refreshRateStr.c_str(), "%f%s", &refreshRate, unit, 8);
#else
    int count = sscanf(refreshRateStr.c_str(), "%f%s", &refreshRate, unit);
#endif
    if (count != 2 || (unit[0] != 'm' && unit[0] != 's')) {
        return {false, 0};
    }
    if (unit[0] == 's') {
        return {true, uint32_t(1000.f * refreshRate)};
    } else if (unit[0] == 'm') {
        return {true, uint32_t(1000.f * 60.f * refreshRate)};
    }
    assert(0);
    return {false, 0};
}

bool parseArgs(const char* prefix, int argc, char** argv) {
    InputParser ip(argc, argv);
    MiningConfig cfg = miningConfig();

    if (ip.cmdOptionExists(OPT_USAGE)) {
        printUsage();
        return false;
    }

    if (ip.cmdOptionExists(OPT_PROXY)) {
        std::string s = ip.getCmdOption(OPT_PROXY);
        if (s.size() > 0) {
            setGlobalProxy(s);
            logLine(prefix, "Using proxy %s", s.c_str());
        } else {
            logLine(prefix, "Invalid proxy value, ignoring it");
        }
    }

    if (ip.cmdOptionExists(OPT_SOLO)) {
        cfg.soloMine = true;
    }

    if (ip.cmdOptionExists(OPT_GPU_IDS)) {
        const auto& gpuIdsStr = ip.getCmdOption(OPT_GPU_IDS);
        std::vector<std::string> gpusToUse = split(gpuIdsStr, ',');
        std::vector<int> gpusToUseIndices;
        std::transform(gpusToUse.begin(), gpusToUse.end(), std::back_inserter(gpusToUseIndices),
                       [](const std::string& str) { return std::stoi(str); });
        // Check if gpusToUseIndices is a subset of gpuIds and all gpu ids are unique
        std::set<int> gpusToUseIndicesSet;
        std::copy(gpusToUseIndices.begin(), gpusToUseIndices.end(), std::inserter(gpusToUseIndicesSet, gpusToUseIndicesSet.end()));
        for (auto&& gpuId : gpusToUseIndicesSet) {
            if (gpuId < 1 || gpuId > cfg.gpuIds.size()) {
                std::cout << "Invalid gpu id in config. GPU id must be between 1 and " << cfg.gpuIds.size() << " (inclusive): " << std::endl;
                return false;
            }
        }

        // Move user input values to new vector and copy that vector to cfg.
        std ::vector<cl_device_id*> gpuIds;
        for (size_t i = 0; i < cfg.gpuIds.size(); i++) {
            // User input was 1-indexed
            if (gpusToUseIndicesSet.find(i + 1) != gpusToUseIndicesSet.end())
                gpuIds.push_back(cfg.gpuIds.at(i));
            else
                free(cfg.gpuIds.at(i));
        }
        cfg.gpuIds = gpuIds;
    }

    if (ip.cmdOptionExists(OPT_REFRESH_RATE)) {
        auto res = parseRefreshRate(ip.getCmdOption(OPT_REFRESH_RATE));
        if (!res.first)
            return false;
        cfg.refreshRateMs = res.second;
    }

    if (ip.cmdOptionExists(OPT_GETWORK_URL)) {
        cfg.getWorkUrl = ip.getCmdOption(OPT_GETWORK_URL);
    }

    if (ip.cmdOptionExists(OPT_FULLNODE_URL)) {
        cfg.fullNodeUrl = ip.getCmdOption(OPT_FULLNODE_URL);
    }

    if (ip.cmdOptionExists(OPT_ARGON)) {
        std::string s = ip.getCmdOption(OPT_ARGON);
        uint32_t t_cost, m_cost, lanes;
        auto count = sscanf(s.c_str(), "%u,%u,%u", &t_cost, &m_cost, &lanes);
        if (count != 3) {
            logLine(prefix, "Warning: invalid %s parameters: %s",
                    OPT_ARGON.c_str(),
                    s.c_str());
        } else {
            setArgonParams(t_cost, m_cost, lanes);
        }
    }

    if (ip.cmdOptionExists(OPT_ARGON_SUBMIT)) {
        forceSubmit();
    }

    setMiningConfig(cfg);

    return true;
}
