file(GLOB EG13_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(eg13_tests ${EG13_TEST_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/alloc_counter.cpp)
find_package(Threads REQUIRED)
target_link_libraries(eg13_tests PRIVATE eg13_core eg13_eurorack Catch2::Catch2WithMain Threads::Threads)
target_include_directories(eg13_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/third_party/Catch2/extras)
include(Catch)
catch_discover_tests(eg13_tests)
