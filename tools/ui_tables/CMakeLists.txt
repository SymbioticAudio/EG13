# Dumps the engine tables (names, P names, value strings) as JSON for the editor's previews and
# tests. Plain C++ over eg13_core: builds on macOS and Linux.
add_executable(eg13_ui_tables dump_ui_tables.cpp)
target_link_libraries(eg13_ui_tables PRIVATE eg13_core)
target_compile_options(eg13_ui_tables PRIVATE -Wall -Wextra -Werror)
