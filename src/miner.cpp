#include "miner.h"

#include <assert.h>
#include <curl/curl.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "args.h"
#include "http.h"
#include "log.h"
#include "miningConfig.h"
#include "timer.h"
#include "updateThread.h"
//#include <unistd.h>

#include <CL/cl.h>
#include <argon2.h>
using namespace rapidjson;
using std::string;
using std::chrono::high_resolution_clock;

#define DEBUG_NONCES (0)

#ifdef _WIN32
// Started to get rare crashes on windows64 when calling RAND_bytes() after enabling static linking for curl/openssl (never happened before when using DLLs ...)
// this seems to happen only when multiple threads call RAND_bytes() at the same time
// only fix found so far is to protect RAND_bytes calls with a mutex ... hoping this will not impact hash rate
// other possible solution: use default C++ random number generator
#define RAND_BYTES_WIN_FIX
#else
#include "_kernel.h"
#endif

typedef struct __clState {
    cl_context context;
    cl_kernel kernel[3];
    size_t n_extra_kernels;
    cl_command_queue commandQueue;
    cl_program program;
    cl_mem outputBuffer;
    cl_mem CLbuffer0;
    cl_mem MidstateBuf;
    cl_mem padbuffer8;
    cl_mem BranchBuffer[4];
    cl_mem Scratchpads;
    cl_mem States;
    cl_mem buffer1;
    cl_mem buffer2;
    cl_mem buffer3;
    cl_mem index_buf[9];
    unsigned char cldata[168];
    bool goffset;
    cl_uint vwidth;
    char hash_order[17];
    size_t max_work_size;
    size_t wsize;
    size_t compute_shaders;
} _clState;

struct MinerInfo {
    MinerInfo() : needRegenSeed(false) {
    }

    MinerInfo(const MinerInfo &origin) : needRegenSeed(false) {
        logPrefix = origin.logPrefix;
    }

    std::atomic<bool> needRegenSeed;
    std::string logPrefix;
};

// atomics shared by miner threads
static std::vector<std::thread *> s_minerThreads;
static std::vector<MinerInfo> s_minerThreadsInfo;
static std::atomic<uint32_t> s_totalHashes(0);
static bool s_bMinerThreadsRun = true;
static std::atomic<uint32_t> s_nBlocksFound(0);
static std::atomic<uint32_t> s_nSharesFound(0);
static std::atomic<uint32_t> s_nSharesAccepted(0);

// use same atomic for miners and update thread http request ID
extern std::atomic<uint32_t> s_nodeReqId;

// TLS storage for miner thread
thread_local Argon2_Context s_ctx;
thread_local Bytes s_seed;
thread_local uint64_t s_nonce = 0;
thread_local uint8_t s_argonHash[ARGON2_HASH_LEN] = {0};
thread_local int s_minerThreadID = {-1};
#pragma message("TODO: what size to use here ?")
thread_local char s_currentWorkHash[256] = {0};
thread_local char s_logPrefix[32] = "MAIN";
thread_local uint64_t s_threadHashes = 0;
thread_local uint64_t s_threadShares = 0;
const size_t PERCENT = 2;

// need to be able to stop main loop from miner threads
extern bool s_run;

// argon2 params for aquachain
const std::vector<int> AQUA_HF7 = {1, 1, 1};

static int AQUA_ARGON_TIME = AQUA_HF7[0];
static int AQUA_ARGON_MEM = AQUA_HF7[1];
static int AQUA_ARGON_LANES = AQUA_HF7[2];

// for controling changes in aqua params
static bool s_argon_prms_mineable = true;
static bool s_argon_prms_forceSubmit = false;
static bool s_argon_prms_sentinel = false;

void mpz_maxBest(mpz_t mpz_n) {
    mpz_t mpz_two, mpz_exponent;
    mpz_init_set_str(mpz_two, "2", 10);
    mpz_init_set_str(mpz_exponent, "256", 10);
    mpz_init_set_str(mpz_n, "0", 10);
    mpz_pow_ui(mpz_n, mpz_two, mpz_get_ui(mpz_exponent));
}

#ifdef RAND_BYTES_WIN_FIX
int threadSafe_RAND_bytes(unsigned char *buf, int num) {
#if 1
    static std::mutex s_nonceGen_mutex;
    s_nonceGen_mutex.lock();
    int res = RAND_bytes(buf, num);
    s_nonceGen_mutex.unlock();
    return res;
#else
    for (int i = 0; i < num; i++)
        buf[i] = rand() & 0xFF;
    return 1;
#endif
}
#define RAND_bytes threadSafe_RAND_bytes
#endif

bool submitEnabled() {
    return s_argon_prms_mineable || s_argon_prms_forceSubmit;
}

uint32_t getTotalSharesSubmitted() {
    return s_nSharesFound;
}

uint32_t getTotalSharesAccepted() {
    return s_nSharesAccepted;
}

uint32_t getTotalHashes() {
    return s_totalHashes;
}

uint32_t getTotalBlocksAccepted() {
    return s_nBlocksFound;
}

#define USE_CUSTOM_ALLOCATOR (1)
#if USE_CUSTOM_ALLOCATOR
static std::mutex s_alloc_mutex;
std::map<std::thread::id, uint8_t *> threadBlocks;

#define USE_STATIC_BLOCKS (1)
int myAlloc(uint8_t **memory, size_t bytes_to_allocate) {
    auto tId = std::this_thread::get_id();
#if USE_STATIC_BLOCKS
    auto it = threadBlocks.find(tId);
    if (it == threadBlocks.end()) {
        s_alloc_mutex.lock();
        {
            threadBlocks[tId] = (uint8_t *)malloc(bytes_to_allocate);
        }
        *memory = threadBlocks[tId];
        s_alloc_mutex.unlock();
    } else {
        *memory = threadBlocks[tId];
    }
#else
    *memory = (uint8_t *)malloc(bytes_to_allocate);
#endif
    assert(*memory);
    return *memory != nullptr;
}

void myFree(uint8_t *memory, size_t bytes_to_allocate) {
#if !USE_STATIC_BLOCKS
    if (memory)
        free(memory);
#endif
}
#endif

void freeCurrentThreadMiningMemory() {
#if USE_CUSTOM_ALLOCATOR
    auto tId = std::this_thread::get_id();
    auto it = threadBlocks.find(tId);
    if (it != threadBlocks.end()) {
        free(threadBlocks[tId]);
        threadBlocks.erase(it);
    }
#endif
}

bool generateAquaSeed(
    uint64_t nonce,
    std::string workHashHex,
    Bytes &seed) {
    const size_t AQUA_NONCE_OFFSET = 32;

    auto hashBytesRes = hexToBytes(workHashHex);
    if (!hashBytesRes.first) {
        seed.clear();
        assert(0);
        return false;
    }
    if (hashBytesRes.second.size() != AQUA_NONCE_OFFSET) {
        seed.clear();
        assert(0);
        return false;
    }

    seed = hashBytesRes.second;
    for (int i = 0; i < 8; i++) {
        seed.push_back(byte(nonce >> (i * 8)) & 0xFF);
    }

    return true;
}

void updateAquaSeed(
    uint64_t nonce,
    Bytes &seed) {
    const size_t AQUA_NONCE_OFFSET = 32;
    for (int i = 0; i < 8; i++) {
        seed[AQUA_NONCE_OFFSET + i] = byte(nonce >> (i * 8)) & 0xFF;
    }
}

void forceSubmit() {
    s_argon_prms_forceSubmit = true;
}

bool argonParamsMineable() {
    return s_argon_prms_mineable;
}

void setArgonParams(long t_cost, long m_cost, long lanes) {
    // will not submit when testing argon params
    s_argon_prms_mineable = (t_cost == AQUA_HF7[0]) && (m_cost == AQUA_HF7[1]) && (lanes == AQUA_HF7[2]);

    // cannot change argon params once we started mining
    if (s_argon_prms_sentinel) {
        printf("Error: setArgonParams called while contexts already created, aborting\n");
        exit(1);
    }

    // set new params globally
    AQUA_ARGON_TIME = t_cost;
    AQUA_ARGON_MEM = m_cost;
    AQUA_ARGON_LANES = lanes;

    // log
    logLine(s_logPrefix, "--- Custom Argon Parameters ---");
    logLine(s_logPrefix, "t_cost        : %u", AQUA_ARGON_TIME);
    logLine(s_logPrefix, "m_cost        : %u", AQUA_ARGON_MEM);
    logLine(s_logPrefix, "lanes         : %u", AQUA_ARGON_LANES);
    logLine(s_logPrefix, "submit shares : %s", submitEnabled() ? "yes" : "no");
}

void setupAquaArgonCtx(
    Argon2_Context &ctx,
    const Bytes &seed,
    uint8_t *outHashPtr) {
    s_argon_prms_sentinel = true;
    memset(&ctx, 0, sizeof(Argon2_Context));
    ctx.out = outHashPtr;
    ctx.outlen = ARGON2_HASH_LEN;
    ctx.pwd = const_cast<uint8_t *>(seed.data());
    assert(seed.size() == 40);
    ctx.pwdlen = 40;
    ctx.salt = NULL;
    ctx.saltlen = 0;
    ctx.version = ARGON2_VERSION_NUMBER;
    ctx.flags = ARGON2_DEFAULT_FLAGS;
    ctx.t_cost = AQUA_ARGON_TIME;
    ctx.m_cost = AQUA_ARGON_MEM;
    ctx.lanes = ctx.threads = AQUA_ARGON_LANES;
#if USE_CUSTOM_ALLOCATOR
    ctx.allocate_cbk = myAlloc;
    ctx.free_cbk = myFree;
#endif
}

std::string nonceToString(uint64_t nonce) {
    // raw hex value
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%" PRIx64, nonce);
    // pad with zeroes
    std::string res(tmp);
    while (res.size() < 16) {
        res = std::string("0") + res;
    }
    // must start with 0x
    res = std::string("0x") + res;
    return res;
}

static std::mutex s_submit_mutex;
static http_connection_handle_t s_httpHandleSubmit = nullptr;

void submitThreadFn(uint64_t nonceVal, std::string hashStr, int minerThreadId) {
    const std::vector<std::string> HTTP_HEADER = {
        "Accept: application/json",
        "Content-Type: application/json"};

    MinerInfo *pMinerInfo = &s_minerThreadsInfo[minerThreadId];

    auto nonceStr = nonceToString(nonceVal);

    // do not submit when testing argon parameters, except if explicitely asked
    if (!submitEnabled()) {
        logLine(pMinerInfo->logPrefix,
                "%s (not submitted, use %s to force), nonce = %s ",
                miningConfig().soloMine ? "Found block !" : "Found share !",
                OPT_ARGON_SUBMIT.c_str(),
                nonceStr.c_str());
        return;
    }

    char submitParams[512] = {0};
    snprintf(
        submitParams,
        sizeof(submitParams),
        "{\"jsonrpc\":\"2.0\", \"id\" : %d, \"method\" : \"aqua_submitWork\", "
        "\"params\" : [\"%s\",\"%s\",\"0x0000000000000000000000000000000000000000000000000000000000000000\"]}",
        ++s_nodeReqId,
        nonceStr.c_str(),
        hashStr.c_str());

    std::string response;
    bool ok = false;

    // all submits are done through the same CURL HTTPP connection
    // so protected with a mutex
    // means that submits will be done sequentially and not in parallel
    s_submit_mutex.lock();
    {
        if (!s_httpHandleSubmit) {
            s_httpHandleSubmit = newHttpConnectionHandle();
        }
        ok = httpPost(
            s_httpHandleSubmit,
            miningConfig().submitWorkUrl.c_str(),
            submitParams, response, &HTTP_HEADER);
    }
    s_submit_mutex.unlock();

    if (!ok) {
        logLine(
            pMinerInfo->logPrefix,
            "\n\n!!! httpPost failed while trying to submit nonce %s!!!\n",
            nonceStr.c_str());
    } else {
        // check that "result" is true
        const char *RESULT = "result";
        Document doc;
        doc.Parse(response.c_str());
        bool accepted = false;
        if (doc.IsObject() && doc.HasMember(RESULT)) {
            if (doc[RESULT].IsString()) {
                accepted = !strcmp(doc[RESULT].GetString(), "true");
            } else if (doc[RESULT].IsBool()) {
                accepted = doc[RESULT].GetBool();
            }
        }

        // log
        if (accepted) {
            logLine(
                pMinerInfo->logPrefix, "%s, nonce = %s",
                miningConfig().soloMine ? "Found block !" : "Found share !",
                nonceStr.c_str());
            s_nSharesAccepted++;
        } else {
            logLine(
                pMinerInfo->logPrefix,
                "\n\n!!! Rejected %s, nonce = %s!!!\n--server response:--\n%s\n",
                miningConfig().soloMine ? "block" : "share",
                nonceStr.c_str(),
                response.c_str());
            pMinerInfo->needRegenSeed = true;
        }
    }
    s_nSharesFound++;
}

static std::mutex s_rand_mutex;

int r() {
    s_rand_mutex.lock();
    int r = rand() % 100;
    s_rand_mutex.unlock();
    return r;
}

bool hash(const WorkParams &p, mpz_t mpz_result, uint64_t nonce, Argon2_Context &ctx) {
    // update the seed with the new nonce
    updateAquaSeed(nonce, s_seed);

    // argon hash
    int res = argon2_ctx(&ctx, Argon2_id);
    if (res != ARGON2_OK) {
        logLine(s_logPrefix, "Error: argon2 failed with code %d", res);
        assert(0);
        return false;
    }

    // convert hash to a mpz (big int)
    // printf("[cpu] outhash = %llx %llx %llx %llx\n", ((uint64_t*)&ctx.out[0])[0], ((uint64_t*)&ctx.out[0])[1], ((uint64_t*)&ctx.out[0])[2], ((uint64_t*)&ctx.out[0])[3]);
    mpz_fromBytesNoInit(ctx.out, ctx.outlen, mpz_result);

    //
    bool needSubmit = mpz_cmp(mpz_result, p.mpz_target) < 0;

    // compare to target
    if (needSubmit) {
        if (miningConfig().soloMine) {
            // for solo mining we do a synchronous submit ASAP
            submitThreadFn(s_nonce, p.hash, s_minerThreadID);
        } else {
            // for pool mining we launch a thread to submit work asynchronously
            // like that we can continue mining while curl performs the request & wait for a response
            std::thread{submitThreadFn, s_nonce, p.hash, s_minerThreadID}.detach();
            s_threadShares++;

            // sleep for a short duration, to allow the submit thread launch its request asap
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}

uint64_t makeAquaNonce() {
    uint64_t nonce = 0;
    auto ok = RAND_bytes((uint8_t *)&nonce, sizeof(nonce));
    assert(ok == 1);
    return nonce;
}
#include <fcntl.h>

#define O_BINARY 0x8000

void get_program_build_log(cl_program program, cl_device_id device) {
    cl_int status;
    size_t ret = 0;

    size_t len = 0;

    ret = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
    char *buffer = (char *)calloc(len, sizeof(char));
    ret = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, len, buffer, NULL);

    if (status == CL_SUCCESS)
        printf("clGetProgramBuildInfo (%d)\n", status);
    printf("%s\n", buffer);
}
#ifdef _WIN32
void load_file(const char *fname, char **dat, size_t *dat_len, int ignore_error) {
    struct stat st;
    int fd;
    ssize_t ret;
    if (-1 == (fd = open(fname, O_RDONLY | O_BINARY))) {
        if (ignore_error)
            return;
        printf("%s: %s\n", fname, strerror(errno));
    }
    if (stat(fd, &st))
        printf("stat: %s: %s\n", fname, strerror(errno));
    *dat_len = st.st_size;
    if (!(*dat = (char *)malloc(*dat_len + 1)))
        printf("malloc: %s\n", strerror(errno));
    ret = read(fd, *dat, *dat_len);
    if (ret < 0)
        printf("read: %s: %s\n", fname, strerror(errno));
    if ((size_t)ret != *dat_len)
        printf("%s: partial read\n", fname);
    if (close(fd))
        printf("close: %s: %s\n", fname, strerror(errno));
    (*dat)[*dat_len] = 0;
}
#endif
void print_device_info(unsigned i, cl_device_id d) {
    char name[1024];
    size_t len = 0;
    int status;
    status = clGetDeviceInfo(d, CL_DEVICE_NAME, sizeof(name), &name, &len);
    if (status != CL_SUCCESS)
        printf("clGetDeviceInfo (%d)\n", status);
    printf("  ID %d: %s\n", i, name);
    fflush(stdout);
}

unsigned scan_platform(cl_platform_id plat, cl_uint *nr_devs_total,
                       cl_platform_id *plat_id, cl_device_id *dev_id, cl_uint *ndevice) {
    cl_device_type typ = CL_DEVICE_TYPE_ALL;
    cl_uint nr_devs = 0;
    cl_device_id *devices;
    cl_int status;
    unsigned found = 0;
    unsigned i;

    status = clGetDeviceIDs(plat, typ, 0, NULL, &nr_devs);
    if (status != CL_SUCCESS)
        printf("clGetDeviceIDs (%d)\n", status);
    if (nr_devs == 0)
        return 0;
    devices = (cl_device_id *)malloc(nr_devs * sizeof(*devices));
    status = clGetDeviceIDs(plat, typ, nr_devs, devices, NULL);
    if (status != CL_SUCCESS)
        printf("clGetDeviceIDs (%d)\n", status);
    i = 0;
    while (i < nr_devs) {
        if (*nr_devs_total == *ndevice) {
            // gpu_to_use++;
            print_device_info(*nr_devs_total, devices[i]);
            found = 1;
            *plat_id = plat;
            *dev_id = devices[i];
            break;
        }
        (*nr_devs_total)++;
        i++;
    }
    free(devices);
    return found;
}

void scan_platforms(cl_platform_id *plat_id, cl_device_id *dev_id, cl_uint *ndevice) {
    cl_uint nr_platforms;
    cl_platform_id *platforms;
    cl_int status;
    status = clGetPlatformIDs(0, NULL, &nr_platforms);
    if (status != CL_SUCCESS)
        printf("Cannot get OpenCL platforms (%d)\n", status);
    if (!nr_platforms) {
        fprintf(stderr, "Found %d OpenCL platform(s), exiting.\n", nr_platforms);
        exit(1);
    }
    platforms = (cl_platform_id *)malloc(nr_platforms * sizeof(*platforms));
    if (!platforms)
        printf("malloc: %s\n", strerror(errno));
    status = clGetPlatformIDs(nr_platforms, platforms, NULL);
    if (status != CL_SUCCESS)
        printf("clGetPlatformIDs (%d)\n", status);

    cl_uint i = 0, nr_devs_total = 0;
    while (i < nr_platforms) {
        if (scan_platform(platforms[i], &nr_devs_total, plat_id, dev_id, ndevice))
            break;
        i++;
    }

    free(platforms);
}

void check_clEnqueueReadBuffer(cl_command_queue queue, cl_mem buffer, cl_bool blocking_read, size_t offset, size_t size, void *ptr, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event) {
    cl_int status;
    status = clEnqueueReadBuffer(queue, buffer, blocking_read, offset,
                                 size, ptr, num_events_in_wait_list, event_wait_list, event);
    if (status != CL_SUCCESS)
        printf("clEnqueueReadBuffer (%d)\n", status);
}

size_t source_len;

void minerThreadFn(int minerID) {
    // Use one of available devices from configuration
    cl_device_id dev_id = *(miningConfig().gpuIds.at(minerID));
    cl_int status;
    __clState cll;
    cll.context = clCreateContext(NULL, 1, &dev_id,
                                  NULL, NULL, &status);

    if (status != CL_SUCCESS || !cll.context)
        printf("clCreateContext (%d)\n", status);

    /* Creating command queue associate with the context.*/
    cll.commandQueue = clCreateCommandQueue(cll.context, dev_id, 0, &status);
    if (status != CL_SUCCESS || !cll.commandQueue)
        printf("clCreateCommandQueue (%d)\n", status);

#if _WIN32
    const *char source = null;
    load_file("input.cl", &source, &source_len, 0);
#else
    const char *source;
    source = ocl_code;
    source_len = strlen(ocl_code);
#endif

    /* Create and build program. */
    cll.program = clCreateProgramWithSource(cll.context, 1, (const char **)&source,
                                            &source_len, &status);
    if (status != CL_SUCCESS || !cll.program)
        printf("clCreateProgramWithSource (%d)\n", status);
    status = clBuildProgram(cll.program, 1, &dev_id,
                            "",  // compile options
                            NULL, NULL);
    if (status != CL_SUCCESS) {
        printf("OpenCL build failed (%d). Build log follows:\n", status);
        get_program_build_log(cll.program, dev_id);
        fflush(stdout);
        exit(1);
    }

    // Create kernel objects ToDo Iterate over kernel arrays
    cll.kernel[0] = clCreateKernel(cll.program, "search", &status);
    if (status != CL_SUCCESS || !cll.kernel[0])
        printf("clCreateKernel-0 (%d)\n", status);
    cll.kernel[1] = clCreateKernel(cll.program, "search1", &status);
    if (status != CL_SUCCESS || !cll.kernel[1])
        printf("clCreateKernel-1 (%d)\n", status);
    cll.kernel[2] = clCreateKernel(cll.program, "search2", &status);
    if (status != CL_SUCCESS || !cll.kernel[2])
        printf("clCreateKernel-2 (%d)\n", status);

#define AR2D_MEM_PER_BATCH 8192
    size_t throughput = 8192 * 4;
    size_t mem_size = throughput * AR2D_MEM_PER_BATCH;
    size_t readbufsize = 128;
    cll.buffer1 = clCreateBuffer(cll.context, CL_MEM_READ_WRITE, mem_size, NULL, &status);
    cll.CLbuffer0 = clCreateBuffer(cll.context, CL_MEM_READ_WRITE, readbufsize, NULL, &status);
    cll.outputBuffer = clCreateBuffer(cll.context, CL_MEM_WRITE_ONLY, 100, NULL, &status);
    uint64_t *pnonces;
    pnonces = (uint64_t *)malloc(sizeof(uint64_t) * 1);

    // record thread id in TLS
    s_minerThreadID = minerID;

    // generate log prefix
    snprintf(s_logPrefix, sizeof(s_logPrefix), "MINER_%02d", minerID);
    s_minerThreadsInfo[minerID].logPrefix.assign(s_logPrefix);

    // init thread TLS variables that need it
    s_seed.resize(40, 0);
    setupAquaArgonCtx(s_ctx, s_seed, s_argonHash);

    // init mpz that will hold result
    // initialization is pretty costly, so should stay here, done only one time
    // (actual value of mpzResult is set by mpz_fromBytesNoInit inside hash() func)
    mpz_t mpz_result;
    mpz_init(mpz_result);

    bool solo = miningConfig().soloMine;

    while (s_bMinerThreadsRun) {
        // get params for current block
        WorkParams prms = currentWorkParams();
        // if params valid
        if (prms.hash.size() != 0) {
            // check if work hash has changed
            if (strcmp(prms.hash.c_str(), s_currentWorkHash)) {
                // generate the TLS nonce & seed nonce again
                s_nonce = makeAquaNonce();

                generateAquaSeed(s_nonce, prms.hash, s_seed);
                // save current hash in TLS
                strcpy(s_currentWorkHash, prms.hash.c_str());
#if DEBUG_NONCES
                logLine(s_logPrefix, "new work starting nonce: %s", nonceToString(s_nonce).c_str());
#endif
            } else {
                if (s_minerThreadsInfo[minerID].needRegenSeed) {
                    // pool has rejected the nonce, record current number of succesfull pool getWork requests
                    uint32_t getWorkCountOfRejectedShare = getPoolGetWorkCount();

                    // generate a new nonce
                    s_nonce = makeAquaNonce();

                    s_minerThreadsInfo[minerID].needRegenSeed = false;
#if DEBUG_NONCES
                    logLine(s_logPrefix, "regen nonce after reject: %s", nonceToString(s_nonce).c_str());
#endif
                    // wait for update thread to get new work
                    if (!solo) {
#define WAIT_NEW_WORK_AFTER_REJECT (1)
#if (WAIT_NEW_WORK_AFTER_REJECT == 0)
                        logLine(s_logPrefix, "regenerated nonce after a reject, not waiting for pool to send new work !");
#else
                        logLine(s_logPrefix, "Thread stopped mining because last share rejected, waiting for new work from pool");
                        while (1) {
                            if (getPoolGetWorkCount() != getWorkCountOfRejectedShare) {
                                break;
                            }
                            std::this_thread::sleep_for(std::chrono::seconds(5));
                        }
                        logLine(s_logPrefix, "Thread resumes mining");
#endif
                    }
                } else {
                    // only inc the TLS nonce
                    s_nonce++;
                }
            }

            // hash
            cl_int status = 0;
            unsigned int num = 0;

            cl_ulong le_target;
            void *some = mpz_export(nullptr, 0, 1, 8, 0, 0, prms.mpz_target);
            le_target = ((uint64_t *)some)[0];
            for (int i = 0; i < 32; i++)
                cll.cldata[i] = s_seed[i];

            status = clEnqueueWriteBuffer(cll.commandQueue, cll.CLbuffer0, true, 0, 32, cll.cldata, 0, NULL, NULL);

            if (status != CL_SUCCESS) {
                printf("EnqueueWriteBuffer failed %d", status);
                exit(1);
            }
            pnonces[0] = 0xffffffffffffffff;
            clEnqueueWriteBuffer(cll.commandQueue, cll.outputBuffer, CL_TRUE, 0, sizeof(uint64_t), pnonces, 0, NULL, NULL);

            // init - search
            clSetKernelArg(cll.kernel[0], 0, sizeof(cl_mem), (void *)&cll.buffer1);
            clSetKernelArg(cll.kernel[0], 1, sizeof(cl_mem), (void *)&cll.CLbuffer0);
            clSetKernelArg(cll.kernel[0], 2, sizeof(uint64_t), &(s_nonce));

            // fill - search 1
            size_t bufferSize = 32 * 8 * 1 * sizeof(cl_uint) * 2;
            uint32_t passes = 1;
            uint32_t lanes = 1;
            uint32_t segment_blocks = 2;

            clSetKernelArg(cll.kernel[1], 0, bufferSize, NULL);
            clSetKernelArg(cll.kernel[1], 1, sizeof(cll.buffer1), (void *)&cll.buffer1);
            clSetKernelArg(cll.kernel[1], 2, sizeof(uint32_t), &passes);
            clSetKernelArg(cll.kernel[1], 3, sizeof(uint32_t), &lanes);
            clSetKernelArg(cll.kernel[1], 4, sizeof(uint32_t), &segment_blocks);

            // final - search 2
            size_t smem = 129 * sizeof(cl_ulong) * 8 + 18 * sizeof(cl_ulong) * 8;
            clSetKernelArg(cll.kernel[2], 0, sizeof(cll.buffer1), (void *)&cll.buffer1);
            clSetKernelArg(cll.kernel[2], 1, sizeof(cll.outputBuffer), (void *)&cll.outputBuffer);
            clSetKernelArg(cll.kernel[2], 2, smem, NULL);
            clSetKernelArg(cll.kernel[2], 3, sizeof(uint64_t), &(s_nonce));
            clSetKernelArg(cll.kernel[2], 4, sizeof(cl_ulong), &le_target);

            const size_t global[1] = {throughput};
            const size_t local[1] = {64};

            status = clEnqueueNDRangeKernel(cll.commandQueue, cll.kernel[0], 1, NULL, global, local, 0, NULL, NULL);

            if (status != CL_SUCCESS) {
                printf("lEnqueueNDRangeKernel[0] (%d). Build log follows:\n", status);
                get_program_build_log(cll.program, dev_id);
                fflush(stdout);
                exit(1);
            }
            clFinish(cll.commandQueue);
            const size_t global2[1] = {throughput * 32};
            const size_t local2[1] = {32};
            status = clEnqueueNDRangeKernel(cll.commandQueue, cll.kernel[1], 1, NULL, global2, local2, 0, NULL, NULL);

            if (status != CL_SUCCESS) {
                printf("lEnqueueNDRangeKernel[1] (%d). Build log follows:\n", status);
                get_program_build_log(cll.program, dev_id);
                fflush(stdout);
                exit(1);
            }
            clFinish(cll.commandQueue);

            const size_t global3[2] = {4, throughput};
            const size_t local3[2] = {4, 8};
            status = clEnqueueNDRangeKernel(cll.commandQueue, cll.kernel[2], 2, NULL, global3, local3, 0, NULL, NULL);

            if (status != CL_SUCCESS) {
                printf("lEnqueueNDRangeKernel[2] (%d). Build log follows:\n", status);
                get_program_build_log(cll.program, dev_id);
                fflush(stdout);
                exit(1);
            }
            clFinish(cll.commandQueue);

            check_clEnqueueReadBuffer(cll.commandQueue, cll.outputBuffer,
                                      CL_TRUE,               // cl_bool blocking_read
                                      0,                     // size_t offset
                                      sizeof(uint64_t) * 1,  // size_t size
                                      pnonces,               // void *ptr
                                      0,                     // cl_uint num_events_in_wait_list
                                      NULL,                  // cl_event *event_wait_list
                                      NULL);

            if (pnonces[0] != 0xffffffffffffffff) {
                s_nonce = pnonces[0];
                hash(prms, mpz_result, s_nonce, s_ctx);
            }
            s_nonce += throughput;
            s_threadHashes += throughput;
            s_totalHashes += throughput;
        }
    }
    freeCurrentThreadMiningMemory();
}

void startMinerThreads(int gpuMiners) {
    assert(gpuMiners > 0);
    assert(s_minerThreads.size() == 0);
    s_minerThreads.resize(gpuMiners);
    s_minerThreadsInfo.resize(gpuMiners);
    for (int i = 0; i < gpuMiners; i++) {
        s_minerThreads[i] = new std::thread(minerThreadFn, i);
    }
}

void stopMinerThreads() {
    assert(s_bMinerThreadsRun);
    s_bMinerThreadsRun = false;
    for (size_t i = 0; i < s_minerThreads.size(); i++) {
        s_minerThreads[i]->join();
        delete s_minerThreads[i];
    }
    s_minerThreads.clear();
    destroyHttpConnectionHandle(s_httpHandleSubmit);
}
