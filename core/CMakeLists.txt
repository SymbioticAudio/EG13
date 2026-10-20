add_library(eg13_core STATIC
  engine/plaits_engine.cpp
  engine/native_rate_wrapper.cpp
  synth/param_table.cpp
  synth/synth.cpp
  state/state.cpp
  voice/voice_allocator.cpp
  voice/voice.cpp
  voice/source_factory.cpp
  part/part.cpp)
target_include_directories(eg13_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eg13_core PRIVATE eg13_eurorack eg13_nlohmann eg13_r8brain)
target_compile_options(eg13_core PRIVATE -Wall -Wextra -Werror)
