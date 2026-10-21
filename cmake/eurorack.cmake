# Upstream Eurorack DSP, compiled unmodified. TEST makes stmlib use portable
# C++ instead of ARM assembly, so every TU that includes these headers needs it.
set(EURORACK_DIR ${CMAKE_SOURCE_DIR}/third_party/eurorack)
set(EG13_EURORACK_PATCHES ${CMAKE_SOURCE_DIR}/third_party/eurorack-patches)

file(GLOB EG13_PLAITS_SOURCES CONFIGURE_DEPENDS
  ${EURORACK_DIR}/plaits/dsp/*.cc
  ${EURORACK_DIR}/plaits/dsp/*/*.cc)

add_library(eg13_eurorack STATIC
  ${EG13_PLAITS_SOURCES}
  ${EURORACK_DIR}/plaits/resources.cc
  ${EURORACK_DIR}/braids/macro_oscillator.cc
  ${EURORACK_DIR}/braids/analog_oscillator.cc
  ${EURORACK_DIR}/braids/digital_oscillator.cc
  ${EURORACK_DIR}/braids/resources.cc
  ${EURORACK_DIR}/elements/dsp/part.cc
  ${EURORACK_DIR}/elements/dsp/voice.cc
  ${EURORACK_DIR}/elements/dsp/ominous_voice.cc
  ${EURORACK_DIR}/elements/dsp/exciter.cc
  ${EURORACK_DIR}/elements/dsp/resonator.cc
  ${EURORACK_DIR}/elements/dsp/string.cc
  ${EURORACK_DIR}/elements/dsp/tube.cc
  ${EURORACK_DIR}/elements/dsp/multistage_envelope.cc
  ${EURORACK_DIR}/elements/resources.cc
  ${EURORACK_DIR}/rings/dsp/part.cc
  ${EURORACK_DIR}/rings/dsp/resonator.cc
  ${EURORACK_DIR}/rings/dsp/string.cc
  ${EURORACK_DIR}/rings/dsp/fm_voice.cc
  ${EURORACK_DIR}/rings/dsp/string_synth_part.cc
  ${EURORACK_DIR}/rings/resources.cc
  ${EURORACK_DIR}/warps/dsp/modulator.cc
  ${EURORACK_DIR}/warps/dsp/oscillator.cc
  ${EURORACK_DIR}/warps/dsp/vocoder.cc
  ${EURORACK_DIR}/warps/dsp/filter_bank.cc
  ${EURORACK_DIR}/warps/resources.cc
  ${EURORACK_DIR}/stmlib/dsp/units.cc
  ${EG13_EURORACK_PATCHES}/stmlib/utils/random.cc)
target_include_directories(eg13_eurorack SYSTEM BEFORE PUBLIC ${EG13_EURORACK_PATCHES})
target_include_directories(eg13_eurorack SYSTEM PUBLIC ${EURORACK_DIR})
target_compile_definitions(eg13_eurorack PUBLIC TEST)
target_compile_options(eg13_eurorack PRIVATE -w)
# Upstream plaits/user_data.h uses printf under TEST without <cstdio> (GCC); keep upstream unmodified.
target_compile_options(eg13_eurorack PRIVATE -include cstdio)
