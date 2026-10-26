# The SwiftUI editor (ui/EG13UI), built by scripts/build-ui.sh into one static library
# (libEG13UI.a, with its symbiotic-design dependencies) and linked into the plugin.
# Defines EG13_HAS_UI and the eg13_ui_swift interface target; link it into whatever must
# contain the editor (eg13-impl, and the tests that call into it). macOS only.

set(EG13_UI_PACKAGE ${CMAKE_SOURCE_DIR}/ui/EG13UI)
set(EG13_UI_OUT_DIR ${CMAKE_BINARY_DIR}/ui)
set(EG13_UI_LIB ${EG13_UI_OUT_DIR}/libEG13UI.a)

# The Swift runtime libraries the static library autolinks come from the SDK and the toolchain
# that built it; /usr/lib/swift is where macOS 12+ keeps them at run time.
execute_process(COMMAND xcrun --show-sdk-path
  OUTPUT_VARIABLE EG13_UI_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND xcrun --find swift
  OUTPUT_VARIABLE EG13_UI_SWIFT_BIN OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
get_filename_component(EG13_UI_TOOLCHAIN_BIN ${EG13_UI_SWIFT_BIN} DIRECTORY)
get_filename_component(EG13_UI_TOOLCHAIN_USR ${EG13_UI_TOOLCHAIN_BIN} DIRECTORY)

# Rebuild whenever anything the package is made of changes (SwiftPM decides what to recompile).
file(GLOB_RECURSE EG13_UI_SOURCES CONFIGURE_DEPENDS
  ${EG13_UI_PACKAGE}/Sources/*.swift ${EG13_UI_PACKAGE}/Sources/*/module.modulemap)
set(EG13_UI_INPUTS ${EG13_UI_SOURCES} ${EG13_UI_PACKAGE}/Package.swift
  ${CMAKE_SOURCE_DIR}/core-api/include/eg13/core_api.h)

set(EG13_UI_ARCHS "${CMAKE_OSX_ARCHITECTURES}")
add_custom_command(OUTPUT ${EG13_UI_LIB}
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/build-ui.sh
    "$<IF:$<CONFIG:Debug>,debug,release>" "${EG13_UI_ARCHS}" ${EG13_UI_OUT_DIR}
  DEPENDS ${EG13_UI_INPUTS}
  COMMENT "Building the SwiftUI editor (ui/EG13UI)"
  VERBATIM)
add_custom_target(eg13_ui DEPENDS ${EG13_UI_LIB})

add_library(eg13_ui_swift INTERFACE)
add_dependencies(eg13_ui_swift eg13_ui)
target_link_libraries(eg13_ui_swift INTERFACE ${EG13_UI_LIB})
target_link_directories(eg13_ui_swift INTERFACE
  ${EG13_UI_SDK_PATH}/usr/lib/swift
  ${EG13_UI_TOOLCHAIN_USR}/lib/swift/macosx)
target_link_options(eg13_ui_swift INTERFACE -Wl,-rpath,/usr/lib/swift)
target_compile_definitions(eg13_ui_swift INTERFACE EG13_HAS_UI=1)

set(EG13_HAS_UI ON)
