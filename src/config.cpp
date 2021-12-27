#include "config.h"

#include "args.h"
#include "miningConfig.h"
#include "string_utils.h"

#ifdef _MSC_VER
#include "windows/procinfo_windows.h"
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

extern std::string s_configDir;

const std::string CONFIG_FILE_NAME = "config.cfg";

std::string readInput() {
    std::string res;
    std::getline(std::cin, res);
    res = trim(res);
    return res;
}

std::string configFilePath() {
    return s_configDir + CONFIG_FILE_NAME;
}

enum {
    MODE = 0,
    GET_WORK_URL,
    GPUIDS,
    REFRESH_RATE_MS,
    FULLNODE_URL,
    N_PARAMS
};

bool configFileExists() {
    std::ifstream fs(configFilePath());
    if (!fs.is_open())
        return false;
    return true;
}

bool loadConfigFile(std::string& log) {
    std::cout << "Reading configuration file" << std::endl;
    MiningConfig newCfg = miningConfig();

    std::ifstream fs(configFilePath());
    // ToDo dont use char arrays, use string.
    const size_t BUFLEN = 1024;  // Pool address can be longer, if we are using merged pools.
    char params[N_PARAMS][BUFLEN];
    for (int i = 0; i < N_PARAMS; i++) {
        if (!fs.getline(params[i], BUFLEN)) {
            log = "not enough lines";
            return false;
        }
        if (i != FULLNODE_URL && (params[i] == nullptr)) {
            log = "empty param";
            return false;
        }
    }

    if (!strcmp(params[MODE], "pool")) {
        newCfg.soloMine = false;
    } else if (!strcmp(params[MODE], "solo")) {
        newCfg.soloMine = true;
    } else {
        log = "cannot find solo/pool mode";
        return false;
    }

    newCfg.getWorkUrl = params[GET_WORK_URL];
    newCfg.fullNodeUrl = params[FULLNODE_URL];

    // Convert string to int.
    std::string input(params[GPUIDS]);
    std::vector<int> gpusToUseIndices;
    try {
        std::vector<std::string> gpusToUse = split(input, ',');
        std::transform(gpusToUse.begin(), gpusToUse.end(), std::back_inserter(gpusToUseIndices),
                       [](const std::string& str) { return std::stoi(str); });
        // Check if gpusToUseIndices is a subset of gpuIds and all gpu ids are unique
        std::set<int> gpusToUseIndicesSet;
        std::copy(gpusToUseIndices.begin(), gpusToUseIndices.end(), std::inserter(gpusToUseIndicesSet, gpusToUseIndicesSet.end()));
        for (auto&& gpuId : gpusToUseIndicesSet) {
            if (gpuId < 1 || gpuId > newCfg.gpuIds.size()) {
                std::cout << "Invalid gpu id in config. GPU id must be between 1 and " << newCfg.gpuIds.size() << " (inclusive): " << std::endl;
                break;
            }
        }

        // Move user input values to new vector and copy that vector to cfg.
        std ::vector<cl_device_id*> gpuIds;
        for (size_t i = 0; i < newCfg.gpuIds.size(); i++) {
            // User input was 1-indexed
            if (gpusToUseIndicesSet.find(i + 1) != gpusToUseIndicesSet.end())
                gpuIds.push_back(newCfg.gpuIds.at(i));
            else
                free(newCfg.gpuIds.at(i));
        }
        newCfg.gpuIds = gpuIds;
    } catch (std::exception& err) {
        std::cout << "Configuration is invalid, has non-integer GPU ids. Will ignore gpu indices from config. " << err.what() << std::endl;
    }

    if (sscanf(params[REFRESH_RATE_MS], "%u", &newCfg.refreshRateMs) != 1) {
        log = "cannot find refresh rate";
        return false;
    }

    setMiningConfig(newCfg);
    return true;
}

bool createConfigFile(std::string& log) {
    MiningConfig newCfg = miningConfig();

    std::cout << std::endl
              << "-- Configuration File creation --" << std::endl;

    bool modeOk = false;
    while (!modeOk) {
        std::cin.clear();
        std::cout << "pool or solo mine ? (pool/solo) ";
        std::string modeStr;
        std::getline(std::cin, modeStr);
        modeOk = true;
        if (modeStr == "pool") {
            newCfg.soloMine = false;
        } else if (modeStr == "solo") {
            newCfg.soloMine = true;
        } else {
            modeOk = false;
            std::cout << "Please answer pool or solo." << std::endl;
        }
    }

    bool getWorkUrlOk = false;
    while (!getWorkUrlOk) {
        std::cin.clear();
        std::cout << (newCfg.soloMine ? "Enter node url (ex: http://127.0.0.1:8543)" : "Enter pool url (ex: http://pool.aquachain-foundation.org:8888/0x1d23de...)")
                  << ", if empty, will pool mine to dev wallet): " << std::endl;
        std::getline(std::cin, newCfg.getWorkUrl);
        newCfg.getWorkUrl = trim(newCfg.getWorkUrl);
        if (newCfg.getWorkUrl.size() == 0) {
            newCfg.getWorkUrl = miningConfig().defaultSubmitWorkUrl;
            newCfg.soloMine = false;
        }
        getWorkUrlOk = true;
    }

    if (newCfg.soloMine) {
        newCfg.fullNodeUrl = newCfg.getWorkUrl;
    } else {
        std::cin.clear();
        std::cout << "Enter node url, ex: http://127.0.0.1:8543 (optional, enter to skip): ";
        newCfg.fullNodeUrl = readInput();
    }

    bool allGpuIdsValid = false;
    std::set<int> gpusToUseIndicesSet;
    while (!allGpuIdsValid) {
        gpusToUseIndicesSet.clear();
        std::cin.clear();

        std::cout << "Gpu devices available: " << std::endl;
        for (size_t i = 0; i < newCfg.gpuIds.size(); i++) {
            size_t valueSize;
            clGetDeviceInfo(*(newCfg.gpuIds.at(i)), CL_DEVICE_NAME, 0, NULL, &valueSize);
            char* value = (char*)malloc(valueSize);
            clGetDeviceInfo(*(newCfg.gpuIds.at(i)), CL_DEVICE_NAME, valueSize, value, NULL);
            printf("Select %ld for Device: %s\n", i + 1, value);
            free(value);
        }

        std::cout << "Enter gpu_ids to use, ex. 1,2,... (optional, enter to use all gpus):";
        std::string input;
        std::getline(std::cin, input);
        if (input.size() == 0) {
            for (size_t i = 0; i < newCfg.gpuIds.size(); i++) {
                gpusToUseIndicesSet.insert(i + 1);  // keep any value facing user as 1-indexed.
            }

            allGpuIdsValid = true;
        } else {
            // Convert string to int.
            std::vector<int> gpusToUseIndices;
            try {
                std::vector<std::string> gpusToUse = split(input, ',');
                std::transform(gpusToUse.begin(), gpusToUse.end(), std::back_inserter(gpusToUseIndices),
                               [](const std::string& str) { return std::stoi(str); });
            } catch (std::exception& err) {
                std::cout << "Enter comma separate integers only: " << err.what() << std::endl;
                continue;
            }

            // Check if gpusToUseIndices is a subset of gpuIds and all gpu ids are unique
            allGpuIdsValid = true;
            std::copy(gpusToUseIndices.begin(), gpusToUseIndices.end(), std::inserter(gpusToUseIndicesSet, gpusToUseIndicesSet.end()));
            for (auto&& gpuId : gpusToUseIndicesSet) {
                if (gpuId < 1 || gpuId > newCfg.gpuIds.size()) {
                    std::cout << "Invalid gpu id. GPU id must be between 1 and " << newCfg.gpuIds.size() << " (inclusive): " << std::endl;
                    allGpuIdsValid = false;
                    break;
                }
            }
            if (!allGpuIdsValid)
                continue;

            // Move user input values to new vector and copy that vector to cfg.
            std ::vector<cl_device_id*> gpuIds;
            for (size_t i = 0; i < newCfg.gpuIds.size(); i++) {
                // User input was 1-indexed
                if (gpusToUseIndicesSet.find(i + 1) != gpusToUseIndicesSet.end())
                    gpuIds.push_back(newCfg.gpuIds.at(i));
                else
                    free(newCfg.gpuIds.at(i));
            }
            newCfg.gpuIds = gpuIds;
        }
        if (!allGpuIdsValid) {
            std::cout << "invalid number of threads" << std::endl;
            return false;
        }
    }

    bool refreshRateOk = false;
    while (!refreshRateOk) {
        std::cin.clear();
        std::cout << "Enter refresh rate, ex: 3s, 2.5m (recommended value is 3s): ";
        std::string refreshRateStr;
        std::getline(std::cin, refreshRateStr);
        auto res = parseRefreshRate(refreshRateStr);
        if (res.first) {
            newCfg.refreshRateMs = res.second;
            refreshRateOk = true;
        } else {
            std::cout << "cannot parse refresh rate" << std::endl;
        }
    }

    std::ofstream fs(configFilePath());
    if (!fs.is_open()) {
        log = "Cannot open config file for writing";
        return false;
    }

    fs << (newCfg.soloMine ? "solo" : "pool") << std::endl;
    fs << newCfg.getWorkUrl << std::endl;
    for (auto&& gpuId : gpusToUseIndicesSet) {
        fs << gpuId << ",";
    }

    fs << std::endl;
    fs << newCfg.refreshRateMs << std::endl;
    fs << newCfg.fullNodeUrl << std::endl;

    fs.close();
    if (!fs) {
        std::cout << "cannot write to " << CONFIG_FILE_NAME << std::endl;
        return false;
    }

    std::cout << std::endl
              << "-- Configuration written to " << CONFIG_FILE_NAME << " --" << std::endl;
    std::cout << "To change config later, either edit " << CONFIG_FILE_NAME << " or delete it and relaunch the miner" << std::endl;
    std::cout << std::endl;

    setMiningConfig(newCfg);

    return true;
}
