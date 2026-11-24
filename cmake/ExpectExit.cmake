# Script mode (cmake -P): run the asteria CLI and require an exact exit code and, optionally,
# a pattern in its output (stdout and stderr together). CTest alone can only tell zero from
# non-zero; the CLI's exit codes 1, 2 and 3 mean different things and are part of its contract.
#
#   cmake -DASTERIA=<asteria.exe> "-DARGS=run|<file>|--max-steps|100" -DEXPECT_EXIT=3
#         [-DEXPECT_OUTPUT=<regex>] [-DEXPECT_NO_FILE=<path>] -P ExpectExit.cmake
#
# ARGS separates arguments with '|' so that paths with spaces or ';' survive.

foreach(var IN ITEMS ASTERIA ARGS EXPECT_EXIT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "ExpectExit.cmake needs -D${var}=...")
    endif()
endforeach()

string(REPLACE "|" ";" args "${ARGS}")
if(DEFINED EXPECT_NO_FILE)
    file(REMOVE "${EXPECT_NO_FILE}")
endif()

execute_process(COMMAND "${ASTERIA}" ${args}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)

if(NOT rc EQUAL EXPECT_EXIT)
    message(FATAL_ERROR "expected exit code ${EXPECT_EXIT}, got ${rc}. Output:\n${out}")
endif()
if(DEFINED EXPECT_OUTPUT AND NOT out MATCHES "${EXPECT_OUTPUT}")
    message(FATAL_ERROR "the output does not match '${EXPECT_OUTPUT}'. Output:\n${out}")
endif()
if(DEFINED EXPECT_NO_FILE AND EXISTS "${EXPECT_NO_FILE}")
    message(FATAL_ERROR "a failed command wrote ${EXPECT_NO_FILE}")
endif()
message(STATUS "exit code ${rc} as expected")
