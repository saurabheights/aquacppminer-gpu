#pragma once

#include <CL/cl.h>
#include <stdint.h>

#include <string>
#include <vector>

struct MiningConfig {
    bool soloMine;
    // List of gpu devices to use.
    std::vector<cl_device_id*> gpuIds;
    uint32_t refreshRateMs;

    std::string getWorkUrl;
    std::string submitWorkUrl;
    std::string submitWorkUrl2;
    std::string fullNodeUrl;
    std::string defaultSubmitWorkUrl;
};

void initMiningConfig();
const MiningConfig& miningConfig();

// do not call that during mining, only during init !
void setMiningConfig(MiningConfig cfg);
