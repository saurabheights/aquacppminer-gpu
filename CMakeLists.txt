# See https://github.com/TheLartians/ModernCppStarter/blob/master/CMakeLists.txt
cmake_minimum_required (VERSION 3.14)

# Setup vcpkg toolchain first. https://github.com/microsoft/vcpkg#vcpkg-as-a-submodule
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake
    CACHE STRING "Vcpkg toolchain file")
set(CMAKE_CXX_STANDARD 17)

project ("aquagpuminer" VERSION 1.0 LANGUAGES CXX C)


add_definitions(-DARCH="Linux")

# ---- Include guards ----
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# Openssl
find_package(OpenSSL REQUIRED)

# GMP
# https://github.com/microsoft/vcpkg/blob/master/docs/examples/installing-and-using-packages.md#handling-libraries-without-native-cmake-support
find_path(GMP_INCLUDE_DIR gmp.h)
include_directories(${GMP_INCLUDE_DIR})
find_library(GMP_LIBRARIES gmp)

# libpthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# OpenCL
find_package(OpenCL 1.2 REQUIRED) 
include_directories(${OpenCL_INCLUDE_DIRS})

# RapidJson
# No need to install, header only library, can be added directly to cmake.
# ToDo Remove this library checkoutand add vcpkg install rapidjson.
# include_directories(${RAPIDJSON_INCLUDE_DIRS})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include/")

find_package(CURL CONFIG REQUIRED)
include_directories(${CURL_INCLUDE_DIR})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/include/)

file(GLOB_RECURSE headers CONFIGURE_DEPENDS 
      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/windows/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/blake2/sse/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/blake2/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/core.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/encoding.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/thread.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/include/argon2.h")
file(GLOB sources CONFIGURE_DEPENDS 
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/windows/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/blake2/sse/blake2b.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/blake2/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/argon2.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/core.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/encoding.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/opt.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/thread.c")

# ---- Create Miner ----
add_executable(${PROJECT_NAME} ${headers} ${sources})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

# ---- Link dependencies ----
target_link_libraries(${PROJECT_NAME} PRIVATE ${GMP_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto CURL::libcurl Threads::Threads OpenCL::OpenCL)