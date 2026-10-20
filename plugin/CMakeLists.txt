add_library(eg13-impl STATIC
  src/descriptors.cpp
  src/eg13_plugin.cpp
  src/entry_impl.cpp)
target_include_directories(eg13-impl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(eg13-impl PUBLIC clap clap-wrapper-extensions eg13_core)
target_compile_definitions(eg13-impl PRIVATE
  EG13_VERSION="${PROJECT_VERSION}"
  EG13_DISPLAY_NAME="${EG13_DISPLAY_NAME}")
target_compile_options(eg13-impl PRIVATE -Wall -Wextra)

make_clapfirst_plugins(
  TARGET_NAME EG13
  IMPL_TARGET eg13-impl
  OUTPUT_NAME "${EG13_DISPLAY_NAME}"
  ENTRY_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/entry.cpp
  BUNDLE_IDENTIFIER "com.symbioticaudio.eg13"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS CLAP VST3 AUV2
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  STANDALONE_CONFIGURATIONS
    standalone "${EG13_DISPLAY_NAME}" "com.symbioticaudio.eg13"
)
