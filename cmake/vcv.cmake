# VCVRack/AudibleInstruments (GPL-3.0-or-later): the Ripples and Shelves models, header-only,
# compiled unmodified through EG13's stand-in for the parts of the Rack SDK they use
# (core/engine/rack_shim/rack.hpp). simde (MIT) supplies the SSE intrinsics on arm64.
add_library(eg13_vcv INTERFACE)
target_include_directories(eg13_vcv SYSTEM INTERFACE
  ${CMAKE_SOURCE_DIR}/third_party/AudibleInstruments/src
  ${CMAKE_SOURCE_DIR}/third_party/simde)
target_include_directories(eg13_vcv INTERFACE ${CMAKE_SOURCE_DIR}/core/engine/rack_shim)
