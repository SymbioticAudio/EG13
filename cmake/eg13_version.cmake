# Writes eg13_version.h (the About screen's version text) from eg13_version.h.in: the project version
# and the git short hash of HEAD ("unknown" outside git). Run at configure time and again on every
# build (the eg13_version target), so an incremental build after a commit shows the new hash.
# configure_file rewrites the header only when its text changes, so an unchanged HEAD recompiles
# nothing.
# Inputs: EG13_SOURCE_DIR, EG13_VERSION_IN, EG13_VERSION_OUT, PROJECT_VERSION.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${EG13_SOURCE_DIR}"
  OUTPUT_VARIABLE EG13_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE EG13_GIT_RESULT)
if(NOT EG13_GIT_RESULT EQUAL 0 OR EG13_GIT_HASH STREQUAL "")
  set(EG13_GIT_HASH "unknown")
endif()
configure_file("${EG13_VERSION_IN}" "${EG13_VERSION_OUT}" @ONLY)
