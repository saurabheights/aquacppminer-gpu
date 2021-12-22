#include "miningConfig.h"

#include <assert.h>

#include <sstream>
#include <vector>

#include "log.h"
#include "updateThread.h"

static MiningConfig s_cfg;

void initMiningConfig() {
    s_cfg.defaultSubmitWorkUrl = "http://127.0.0.1:8543";
    s_cfg.getWorkUrl = s_cfg.defaultSubmitWorkUrl;
    s_cfg.soloMine = false;
    s_cfg.nThreads = 0;
    s_cfg.refreshRateMs = 3000;
}

void setMiningConfig(MiningConfig cfg) {
    // set submit work url
    assert(cfg.getWorkUrl.size() > 0);
    cfg.submitWorkUrl = cfg.getWorkUrl;

    // solo, submit & request urls are the same
    if (cfg.soloMine) {
        cfg.fullNodeUrl = cfg.submitWorkUrl;
    }

    // set globally
    s_cfg = cfg;
}

const MiningConfig& miningConfig() {
    return s_cfg;
}
