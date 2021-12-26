# See https://github.com/TheLartians/ModernCppStarter/blob/master/CMakeLists.txt
cmake_minimum_required (VERSION 3.14)
project ("aquagpuminer" VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)

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
find_package(PkgConfig REQUIRED)
pkg_check_modules(GMP REQUIRED gmp)
include_directories(${GMP_INCLUDE_DIR})

# libpthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# OpenCL
find_package(OpenCL 1.2 REQUIRED) 
include_directories(${OpenCL_INCLUDE_DIRS})

# RapidJson
# find_package(RapidJSON CONFIG REQUIRED)  # No need to install until we get vcpkg. header only library, can be added directly to cmake.
# include_directories(${RAPIDJSON_INCLUDE_DIRS})
# ToDo Remove this library checkoutand add vcpkg install rapidjson.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include/")

set(CURL_LIBRARY "-lcurl") 
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIR})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/include/)

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
# ToDo Add git submodules for blake2 and phc-winner-argon2
file(GLOB_RECURSE headers CONFIGURE_DEPENDS 
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/blake2/sse/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/blake2/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/core.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/encoding.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/src/thread.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/phc-winner-argon2/include/argon2.h")
file(GLOB sources CONFIGURE_DEPENDS 
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
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
# being a cross-platform target, we enforce standards conformance on MSVC
#target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE ${GMP_LIBRARIES} OpenSSL::SSL ${CURL_LIBRARIES} Threads::Threads ${OpenCL_LIBRARIES})

# For debugging
# message(STATUS "\n All cmake variables below:-\n")
# get_cmake_property(_variableNames VARIABLES)
# list(SORT _variableNames)
# foreach(_variableName ${_variableNames})
#     message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()