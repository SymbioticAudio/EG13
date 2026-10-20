# Upstream Eurorack DSP, compiled unmodified. TEST makes stmlib use portable
# C++ instead of ARM assembly, so every TU that includes these headers needs it.
set(EURORACK_DIR ${CMAKE_SOURCE_DIR}/third_party/eurorack)

file(GLOB EG13_PLAITS_SOURCES CONFIGURE_DEPENDS
  ${EURORACK_DIR}/plaits/dsp/*.cc
  ${EURORACK_DIR}/plaits/dsp/*/*.cc)

add_library(eg13_eurorack STATIC
  ${EG13_PLAITS_SOURCES}
  ${EURORACK_DIR}/plaits/resources.cc
  ${EURORACK_DIR}/stmlib/dsp/units.cc
  ${EURORACK_DIR}/stmlib/utils/random.cc)
target_include_directories(eg13_eurorack SYSTEM PUBLIC ${EURORACK_DIR})
target_compile_definitions(eg13_eurorack PUBLIC TEST)
target_compile_options(eg13_eurorack PRIVATE -w)
