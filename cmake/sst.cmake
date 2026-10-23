# surge-synthesizer/sst-filters and sst-basic-blocks (GPL-3.0-or-later), header-only, at their
# last_cpp_17 tags. sst-basic-blocks' simd/setup.h includes simde (MIT) on every platform.
add_library(eg13_sst INTERFACE)
target_include_directories(eg13_sst SYSTEM INTERFACE
  ${CMAKE_SOURCE_DIR}/third_party/sst-filters/include
  ${CMAKE_SOURCE_DIR}/third_party/sst-basic-blocks/include
  ${CMAKE_SOURCE_DIR}/third_party/simde)
