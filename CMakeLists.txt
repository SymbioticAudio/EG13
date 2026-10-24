cmake_minimum_required(VERSION 3.21)

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(EG13 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(EG13_DISPLAY_NAME "EG13" CACHE STRING "Plugin display name shown in hosts")
option(EG13_BUILD_PLUGIN "Build CLAP/AU/VST3/Standalone (macOS)" ${APPLE})
option(EG13_BUILD_TESTS "Build the test suite" ON)

include(cmake/eurorack.cmake)
include(cmake/r8brain.cmake)
include(cmake/vcv.cmake)
include(cmake/sst.cmake)

add_library(eg13_nlohmann INTERFACE)
target_include_directories(eg13_nlohmann SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/third_party/nlohmann)

add_subdirectory(core)

# The UI bridge (edit queue to the synth and the host) has no Apple code, so it builds and is
# tested on Linux too. The CLAP headers are all it needs from the plugin side.
add_subdirectory(third_party/clap EXCLUDE_FROM_ALL)
add_library(eg13_ui_bridge STATIC plugin/src/ui_bridge.cpp)
target_include_directories(eg13_ui_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin/src)
target_link_libraries(eg13_ui_bridge PUBLIC clap eg13_core)
target_compile_options(eg13_ui_bridge PRIVATE -Wall -Wextra)

if(EG13_BUILD_TESTS)
  enable_testing()
  add_subdirectory(third_party/Catch2)
  add_subdirectory(tests)
endif()

if(EG13_BUILD_PLUGIN)
  if(APPLE)
    # clap-wrapper targets created from plugin/ compile ObjC++ sources.
    enable_language(OBJC)
    enable_language(OBJCXX)
  endif()
  set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "Fetch VST3 and AudioUnit SDKs")
  set(CLAP_WRAPPER_BUILD_AUV2 TRUE CACHE BOOL "Build the AUv2 wrapper")
  add_subdirectory(third_party/clap-wrapper)
  add_subdirectory(plugin)
endif()
