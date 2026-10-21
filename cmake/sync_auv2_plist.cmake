# Copies the clap-wrapper generated AU Info.plist into the built .component
# bundle (only if different) and fails unless the bundle's plist is a valid
# plist carrying AudioComponents. Run with:
#   cmake -DSRC=<auv2_Info.plist> -DDEST=<bundle>/Contents/Info.plist -P sync_auv2_plist.cmake
foreach(var SRC DEST)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "sync_auv2_plist: ${var} not set")
  endif()
endforeach()

if(NOT EXISTS "${SRC}")
  message(FATAL_ERROR "sync_auv2_plist: generated plist missing: ${SRC}")
endif()

get_filename_component(dest_dir "${DEST}" DIRECTORY)
file(MAKE_DIRECTORY "${dest_dir}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DEST}"
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sync_auv2_plist: could not copy ${SRC} to ${DEST}")
endif()

execute_process(COMMAND plutil -lint "${DEST}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sync_auv2_plist: plutil -lint failed for ${DEST}: ${out}")
endif()

execute_process(COMMAND plutil -extract AudioComponents raw -o - "${DEST}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE count ERROR_VARIABLE out)
string(STRIP "${count}" count)
if(NOT rc EQUAL 0 OR count LESS 1)
  message(FATAL_ERROR "sync_auv2_plist: ${DEST} has no AudioComponents entries (${out})")
endif()
