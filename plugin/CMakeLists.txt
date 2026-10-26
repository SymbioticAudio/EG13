add_library(eg13-impl STATIC
  src/descriptors.cpp
  src/eg13_plugin.cpp
  src/entry_impl.cpp)
target_include_directories(eg13-impl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(eg13-impl PUBLIC clap clap-wrapper-extensions eg13_core eg13_ui_bridge eg13_core_api)
if(EG13_HAS_UI)
  target_link_libraries(eg13-impl PUBLIC eg13_gui eg13_ui_swift)
endif()
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

# clap-wrapper generates the AU Info.plist by loading the built .clap, but its
# helper target only has an order-only dependency on it. Relink the helper
# whenever the .clap changes so the plist never goes stale.
if(TARGET EG13_auv2-build-helper)
  set_property(TARGET EG13_auv2-build-helper APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:EG13_clap>)
endif()

# The AU bundle's Info.plist must be the generated auv2_Info.plist. Two things
# leave a generic one (no AudioComponents) behind: CMake itself writes a
# generic Contents/Info.plist into every BUNDLE target at generate time, so
# any reconfigure clobbers it without a relink; and clap-wrapper copies the
# generated plist only in a PRE_BUILD step (PRE_LINK on Ninja/Make), which runs
# only on a relink. So: relink when the generated plist changes, re-sync and
# verify after every link, and re-sync on every build via the always-run
# EG13_auv2_plist target under EG13_all. That target is what repairs a
# reconfigure, so it must not be removed. Build EG13_all, not EG13_auv2 alone.
if(TARGET EG13_auv2)
  set(EG13_AUV2_PLIST ${CMAKE_CURRENT_BINARY_DIR}/EG13_auv2-build-helper-output/auv2_Info.plist)
  set(EG13_AUV2_SYNC ${CMAKE_COMMAND}
    -DSRC=${EG13_AUV2_PLIST}
    -DDEST=$<TARGET_BUNDLE_CONTENT_DIR:EG13_auv2>/Info.plist
    -P ${PROJECT_SOURCE_DIR}/cmake/sync_auv2_plist.cmake)
  set_property(TARGET EG13_auv2 APPEND PROPERTY LINK_DEPENDS ${EG13_AUV2_PLIST})
  add_custom_command(TARGET EG13_auv2 POST_BUILD COMMAND ${EG13_AUV2_SYNC}
    COMMENT "Syncing and verifying AU Info.plist" VERBATIM)
  add_custom_target(EG13_auv2_plist ALL COMMAND ${EG13_AUV2_SYNC}
    COMMENT "Verifying AU Info.plist" VERBATIM)
  add_dependencies(EG13_auv2_plist EG13_auv2)
  if(TARGET EG13_all)
    add_dependencies(EG13_all EG13_auv2_plist)
  endif()
endif()
