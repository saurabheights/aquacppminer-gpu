#!/bin/bash
clear
set -e

pwd=$(pwd)

function LOG_SECTION {
	echo
	echo "------------------------ $1"
	echo
}

LOG_SECTION "CLEANUP"
rm -rf "bin"
rm -rf "prj"
rm -rf "tools"

cd $pwd
LOG_SECTION "PREMAKE"
bash build/scripts_mac/setup_premake.sh

cd $pwd
LOG_SECTION "GENERATE MAKEFILE"
bash build/scripts_linux/gen_prj.sh

LOG_SECTION "SETUP SUCCESS !"
