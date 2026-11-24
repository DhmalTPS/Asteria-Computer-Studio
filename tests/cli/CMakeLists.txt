# The asteria CLI's contract: exit codes and the key line of each outcome.
#   0  the program halted with a0 = 0        1  it halted with a0 != 0
#   2  bad usage or bad input                3  it did not halt (step limit, trap loop, WFI)

function(asteria_cli_test name expect_exit expect_output)
    cmake_parse_arguments(PARSE_ARGV 3 ARG "" "NO_FILE" "ARGS")
    list(JOIN ARG_ARGS "|" joined)
    set(extra "")
    if(ARG_NO_FILE)
        set(extra "-DEXPECT_NO_FILE=${ARG_NO_FILE}")
    endif()
    add_test(NAME cli/${name}
             COMMAND "${CMAKE_COMMAND}" "-DASTERIA=$<TARGET_FILE:asteria>" "-DARGS=${joined}"
                     "-DEXPECT_EXIT=${expect_exit}" "-DEXPECT_OUTPUT=${expect_output}" ${extra}
                     -P "${PROJECT_SOURCE_DIR}/cmake/ExpectExit.cmake")
endfunction()

set(dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(out "${CMAKE_CURRENT_BINARY_DIR}")

asteria_cli_test(pass 0 "halted with exit code 0"
    ARGS run "${PROJECT_SOURCE_DIR}/examples/sum.asm")
asteria_cli_test(exit_nonzero 1 "halted with exit code 3"
    ARGS run "${dir}/exit_nonzero.asm")
asteria_cli_test(bad_syntax 2 "bad_syntax.asm:2:9: error: unknown instruction 'FOO'.*bad_syntax.asm:3:"
    ARGS build "${dir}/bad_syntax.asm" -o "${out}/bad_syntax.ast"
    NO_FILE "${out}/bad_syntax.ast")
asteria_cli_test(missing_file 2 "missing.asm"
    ARGS run "${dir}/missing.asm")
asteria_cli_test(no_command 2 "usage: asteria"
    ARGS)
asteria_cli_test(unknown_command 2 "unknown command 'frobnicate'"
    ARGS frobnicate)
asteria_cli_test(trap_loop 3 "stuck in a trap loop at 0x00000000: IllegalInstruction.*first trap was at 0x80000004: LoadMisaligned"
    ARGS run "${dir}/trap_loop.asm")
asteria_cli_test(step_limit 3 "the step limit of 1000 was reached"
    ARGS run "${dir}/forever.asm" --max-steps 1000)
asteria_cli_test(wait_forever 3 "waits for an interrupt, and no device can raise one"
    ARGS run "${dir}/wait.asm")
asteria_cli_test(bad_max_steps 2 "max-steps"
    ARGS run "${dir}/forever.asm" --max-steps lots)
asteria_cli_test(help 0 "usage: asteria"
    ARGS help)
asteria_cli_test(disasm_not_axf 2 "not an AXF file: bad magic number"
    ARGS disasm "${dir}/not_axf.ast")
asteria_cli_test(run_not_axf 2 "not an AXF file: bad magic number"
    ARGS run "${dir}/not_axf.ast")
