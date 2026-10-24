file(GLOB EG13_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(eg13_tests ${EG13_TEST_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/alloc_counter.cpp)
find_package(Threads REQUIRED)
target_link_libraries(eg13_tests PRIVATE eg13_core eg13_eurorack eg13_r8brain eg13_vcv eg13_sst Catch2::Catch2WithMain Threads::Threads eg13_nlohmann)
target_compile_definitions(eg13_tests PRIVATE EG13_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_include_directories(eg13_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Built as core/engine/beads_engine.cpp is (no FMA contraction), so the restated Granular Delay
# reference rounds like the engine on every compiler.
set_source_files_properties(test_beads_engine.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)

# The tests' reference copies of the VCV models, built with Rack's flags as VCV ships them (and as
# EG13 builds its own copies, core/CMakeLists.txt), each in its own TU inside
# eg13::test::rack_flags.
add_library(eg13_vcv_flagged_reference OBJECT reference/flagged_ripples.cpp reference/flagged_shelves.cpp)
target_include_directories(eg13_vcv_flagged_reference PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eg13_vcv_flagged_reference PRIVATE eg13_vcv)
target_compile_options(eg13_vcv_flagged_reference PRIVATE -Wall -Wextra -Werror -O3 -funsafe-math-optimizations)
target_sources(eg13_tests PRIVATE $<TARGET_OBJECTS:eg13_vcv_flagged_reference>)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/third_party/Catch2/extras)
include(Catch)
catch_discover_tests(eg13_tests)

# Preflight ruling F6: code built with Rack's unsafe-math flags must not share a symbol with code
# built without them (the linker would keep one copy for both).
add_test(NAME "flagged VCV models define symbols only in their rack_flags namespace"
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-flagged-symbols.sh
    eg13::rack_flags:: $<TARGET_OBJECTS:eg13_vcv_flagged>
    eg13::test::rack_flags:: $<TARGET_OBJECTS:eg13_vcv_flagged_reference>)
# The check fails closed when the build hands it nothing to check.
add_test(NAME "flagged symbol check fails on an empty object list"
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-flagged-symbols.sh eg13::rack_flags:: "\;")
set_tests_properties("flagged symbol check fails on an empty object list" PROPERTIES WILL_FAIL TRUE)
