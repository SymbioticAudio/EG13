add_library(eg13_core STATIC
  engine/plaits_engine.cpp
  synth/param_table.cpp
  synth/synth.cpp
  state/state.cpp)
target_include_directories(eg13_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eg13_core PRIVATE eg13_eurorack eg13_nlohmann)
target_compile_options(eg13_core PRIVATE -Wall -Wextra -Werror)
