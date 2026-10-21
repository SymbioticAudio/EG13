add_library(eg13_core STATIC
  engine/plaits_engine.cpp
  engine/braids_engine.cpp
  engine/elements_engine.cpp
  engine/rings_engine.cpp
  engine/audio_input_engine.cpp
  engine/engine_registry.cpp
  engine/native_rate_wrapper.cpp
  synth/param_table.cpp
  synth/synth.cpp
  state/state.cpp
  voice/voice_allocator.cpp
  voice/voice.cpp
  voice/source_factory.cpp
  part/part.cpp)
target_include_directories(eg13_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eg13_core PRIVATE eg13_eurorack eg13_nlohmann eg13_r8brain eg13_vcv eg13_sst)
target_compile_options(eg13_core PRIVATE -Wall -Wextra -Werror)
