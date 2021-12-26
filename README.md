# Multi-GPU variety of [aquacppminer](https://github.com/aquachain/aquacppminer)
# AquaGpuMiner
[Aquachain](https://aquachain.github.io/) optimized gpu miner. Currently the best miner for mining AQUA.

**[Tutorial](https://telegra.ph/Mining-AQUA-05-27)**
# Versions
* 1.0: initial release
* 1.1: fix occasional bad shares, linux/win/macOS build scripts
* 1.2: less HTTP connections, --proxy option, developer options, reduced fee to 2%
* 1.3.1: fee system removed
* 1.4.0: Added multi-gpu support.

# Setup

## Linux

### Install dependencies

For linux(ubuntu), we have also provided a dockerfile for nvidia-gpu cards. See `./docker/README.md` for building with docker.

To build with docker, first install dependencies to build miner:

``` shell
sudo apt-get install -y \
    git build-essential pkg-config cmake curl \
    libssl-dev libcurl4-gnutls-dev libgmp-dev \
    ocl-icd-libopencl1 ocl-icd-opencl-dev clinfo
```

### Build

Clone the repo to any directory.
```shell
git clone --recurse-submodules https://github.com/saurabheights/aquagpuminer.git
mkdir aquagpuminer/build
cd aquagpuminer/build
cmake ..
make -j4
```

### Config file
* First time you launch the miner it will ask for configuration and store it into config.cfg. 
* You can edit this file later if you want, delete config.cfg and relaunch the miner to reset configuration
* If using commandline parameters (see next section) miner will not create config file.
* Commandline parameters have priority over config file.

### Usage

``` shell
aquacppminer.exe -F url [-g gpu_id1,gpu_id2,...] [-n nodeUrl] [--solo] [-r refreshRate] [-h]
  -F url         : url of pool or node to mine on, if not specified, will pool mine to dev's aquabase
  -g id1,id2,... : Commo separate list of gpu ids to use, ex: -g 1,2. By default, uses all gpus available.
  -n node_url    : optional node url, to get more stats (pool mining only)
  -r rate        : pool refresh rate, ex: 3s, 2.5m, default is 3s
  --solo         : solo mining, -F needs to be the node url
  --proxy        : proxy to use, ex: --proxy socks5://127.0.0.1:9150  --argon x,y,z  : use specific argon params (ex: 4,512,1), skip shares submit if incompatible with HF7
  --submit       : when used with --argon, forces submitting shares to pool/node
  -h             : display this help message and exit
```
### Examples

Pool Mining - Use all available gpus

```
aquagpuminer -F http://aqua.signal2noi.se:19998/0x05935dCE74Df570C9bC0212e0142DbC6D0E63999/1
```

Pool Mining - Use all gpus and getting more block stats from local aqua node

```
aquagpuminer -F http://pool.aquachain-foundation.org:8888/0x6e37abb108f4010530beb4bbfd9842127d8bfb3f -n http://127.0.0.1:8543
```

Solo Mining to local aqua node, use all gpus:-

```
aquagpuminer --solo -F http://127.0.0.1:8543
```
### Credits
* Twitter: [@aquacrypto](https://twitter.com/aquacrypto)
* Discord: saurabheights#4094
* AQUA : 0x299D94e66d17137e2B5b96527cA146FA6f0b4c24
