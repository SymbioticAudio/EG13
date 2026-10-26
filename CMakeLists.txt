cmake_minimum_required(VERSION 3.21)

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(EG13 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(EG13_DISPLAY_NAME "EG13" CACHE STRING "Plugin display name shown in hosts")
option(EG13_BUILD_PLUGIN "Build CLAP/AU/VST3/Standalone (macOS)" ${APPLE})
option(EG13_BUILD_TESTS "Build the test suite" ON)
option(EG13_BUILD_UI "Build the SwiftUI editor (ui/EG13UI) into the plugin" ${APPLE})

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

# The C function table the editors use (core-api/include is the pure-C public header).
add_library(eg13_core_api STATIC plugin/src/core_api.cpp)
target_include_directories(eg13_core_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin/src
  ${CMAKE_CURRENT_SOURCE_DIR}/core-api/include)
target_link_libraries(eg13_core_api PUBLIC eg13_ui_bridge eg13_core)
target_compile_options(eg13_core_api PRIVATE -Wall -Wextra)

# The SwiftUI editor (macOS only): the eg13_ui target and the eg13_ui_swift link interface.
if(EG13_BUILD_UI)
  if(NOT APPLE)
    message(FATAL_ERROR "EG13_BUILD_UI needs macOS")
  endif()
  include(cmake/eg13_ui.cmake)

  # The editor window (CLAP gui extension logic); linked into the plugin and the tests.
  add_library(eg13_gui STATIC plugin/src/gui.cpp)
  target_include_directories(eg13_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core-api/include)
  target_link_libraries(eg13_gui PUBLIC eg13_ui_bridge eg13_ui_swift)
  target_compile_options(eg13_gui PRIVATE -Wall -Wextra)
endif()

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
