# ctest ui_tables_up_to_date: regenerates the tables and diffs them against the committed file.
#   cmake -DTOOL=<eg13_ui_tables> -DGOLDEN=<core-tables.json> -DOUT=<temp.json> -P check_ui_tables.cmake
execute_process(COMMAND ${TOOL} ${OUT} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "eg13_ui_tables failed (${result})")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${GOLDEN} ${OUT} RESULT_VARIABLE differs)
if(NOT differs EQUAL 0)
  # The diff is a convenience; without a diff tool the message still says how to regenerate.
  execute_process(COMMAND diff -u ${GOLDEN} ${OUT} OUTPUT_VARIABLE changes ERROR_QUIET)
  string(SUBSTRING "${changes}" 0 4000 changes)
  message(FATAL_ERROR "${GOLDEN} is out of date. Regenerate it:\n  ${TOOL} ${GOLDEN}\n${changes}")
endif()
