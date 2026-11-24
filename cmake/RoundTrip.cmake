# Script mode (cmake -P): assemble -> disassemble -> reassemble one program, and require the two
# executables to be byte-identical. This is the M1 "round trip" exit criterion, run per program.
#
#   cmake -DASTERIA=<asteria.exe> -DSOURCE=<file.asm> -DWORK_DIR=<dir> -P RoundTrip.cmake

foreach(var IN ITEMS ASTERIA SOURCE WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "RoundTrip.cmake needs -D${var}=...")
    endif()
endforeach()

get_filename_component(name "${SOURCE}" NAME_WE)
file(MAKE_DIRECTORY "${WORK_DIR}")
set(first   "${WORK_DIR}/${name}.ast")
set(listing "${WORK_DIR}/${name}.listing.asm")
set(second  "${WORK_DIR}/${name}.again.ast")
file(REMOVE "${first}" "${listing}" "${second}")

function(run_step what)
    execute_process(COMMAND "${ASTERIA}" ${ARGN}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${what} failed (exit code ${rc}):\n${out}")
    endif()
endfunction()

run_step("assembling ${SOURCE}"      build  "${SOURCE}"  -o "${first}")
run_step("disassembling ${first}"    disasm "${first}"   -o "${listing}")
run_step("reassembling the listing"  build  "${listing}" -o "${second}")

execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${first}" "${second}"
                RESULT_VARIABLE differ)
if(NOT differ EQUAL 0)
    message(FATAL_ERROR "the round trip changed the bytes: ${first} and ${second} differ "
                        "(listing: ${listing})")
endif()
message(STATUS "round trip of ${name}: identical")
