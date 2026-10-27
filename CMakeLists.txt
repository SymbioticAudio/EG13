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

# Version text for the About screen: the project version and the git short hash, fixed at configure
# time ("unknown" outside git).
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE EG13_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE EG13_GIT_RESULT)
if(NOT EG13_GIT_RESULT EQUAL 0 OR EG13_GIT_HASH STREQUAL "")
  set(EG13_GIT_HASH "unknown")
endif()
configure_file(cmake/eg13_version.h.in ${CMAKE_BINARY_DIR}/generated/eg13_version.h @ONLY)

# The C function table the editors use (core-api/include is the pure-C public header).
add_library(eg13_core_api STATIC plugin/src/core_api.cpp)
target_include_directories(eg13_core_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin/src
  ${CMAKE_CURRENT_SOURCE_DIR}/core-api/include)
target_include_directories(eg13_core_api PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(eg13_core_api PUBLIC eg13_ui_bridge eg13_core)
target_compile_options(eg13_core_api PRIVATE -Wall -Wextra)
# The factory-preset functions of the table, apart from eg13_core_api because the generated factory
# table (below) needs the preset tool, and the tool uses eg13_core_api.
add_library(eg13_core_api_factory STATIC plugin/src/core_api_factory.cpp)
target_link_libraries(eg13_core_api_factory PUBLIC eg13_core_api eg13_preset_factory)
target_compile_options(eg13_core_api_factory PRIVATE -Wall -Wextra)

# The preset tool, and the factory preset table it generates from presets/ at build time (an empty
# table while a directory is missing or empty). The table is linked into the plugin and the tests.
add_subdirectory(tools/preset_tool)
set(EG13_FACTORY_PRESETS_CPP ${CMAKE_BINARY_DIR}/generated/factory_presets.cpp)
# A target rather than an OUTPUT rule: a preset deleted from presets/ must regenerate the table too,
# and a dependency list cannot see a file that is gone. The tool rewrites the file only when its
# content changes, so an unchanged table recompiles nothing.
add_custom_target(eg13_factory_table
  COMMAND eg13_preset table
    ${CMAKE_SOURCE_DIR}/presets/bank1 ${CMAKE_SOURCE_DIR}/presets/bank2-catnap ${CMAKE_SOURCE_DIR}/presets/fx
    -o ${EG13_FACTORY_PRESETS_CPP}
  BYPRODUCTS ${EG13_FACTORY_PRESETS_CPP}
  DEPENDS eg13_preset
  COMMENT "Compiling the factory presets"
  VERBATIM)
add_library(eg13_preset_factory STATIC core/preset/factory.cpp ${EG13_FACTORY_PRESETS_CPP})
add_dependencies(eg13_preset_factory eg13_factory_table)
target_link_libraries(eg13_preset_factory PUBLIC eg13_core)
target_compile_options(eg13_preset_factory PRIVATE -Wall -Wextra -Werror)

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
  add_subdirectory(tools/ui_tables)  # tests only: it feeds the check below

  # The editor's engine tables (names and value strings) are generated from the core and committed;
  # this fails when an engine changed without regenerating them.
  add_dependencies(eg13_tests eg13_ui_tables)
  add_test(NAME ui_tables_up_to_date
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:eg13_ui_tables>
      -DGOLDEN=${CMAKE_SOURCE_DIR}/ui/EG13UI/Sources/EG13UIFixtures/core-tables.json
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/ui_tables_up_to_date.json
      -P ${CMAKE_SOURCE_DIR}/tools/ui_tables/check_ui_tables.cmake)
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
