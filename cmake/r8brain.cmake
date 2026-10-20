# r8brain-free-src 7.6 (MIT): header-only sample-rate converter.
find_package(Threads REQUIRED)
add_library(eg13_r8brain INTERFACE)
target_include_directories(eg13_r8brain SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/third_party/r8brain-free-src)
target_link_libraries(eg13_r8brain INTERFACE Threads::Threads)
