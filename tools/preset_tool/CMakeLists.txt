# The preset tool: readable preset sources to preset files and the embedded factory table, WAV
# renders, golden level envelopes. Plain C++ over eg13_core (and the C API's name functions): builds
# on macOS and Linux. eg13_preset_lib is shared with the tests.
add_library(eg13_preset_lib STATIC preset_lib.cpp)
target_include_directories(eg13_preset_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(eg13_preset_lib PUBLIC eg13_core eg13_core_api eg13_nlohmann)
target_compile_options(eg13_preset_lib PRIVATE -Wall -Wextra -Werror)

add_executable(eg13_preset eg13_preset.cpp)
target_link_libraries(eg13_preset PRIVATE eg13_preset_lib)
target_compile_definitions(eg13_preset PRIVATE
  EG13_PRESETS_DIR="${CMAKE_SOURCE_DIR}/presets"
  EG13_GOLDEN_FILE="${CMAKE_SOURCE_DIR}/tests/data/preset_golden.json")
target_compile_options(eg13_preset PRIVATE -Wall -Wextra -Werror)
