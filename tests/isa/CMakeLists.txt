# The ISA conformance suite (M1 part 4). Every program checks itself: it halts with a0 = 0
# when all its checks pass, or with a0 = the number of the first failed check, which makes
# `asteria run` exit with code 1. A program that never halts exits with 3. See README.md.
#
# Each program is also a round-trip test: assemble -> disassemble -> reassemble must give
# byte-identical executables.

set(ISA_PROGRAMS
    alu
    alu_imm
    muldiv
    memory
    branch
    jump
    csr
    traps
    paging
    illegal
)

# A conformance program is short; a runaway one should fail fast, not after 10 million steps.
set(ISA_MAX_STEPS 100000)

foreach(program IN LISTS ISA_PROGRAMS)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${program}.asm")
    add_test(NAME conformance/${program}
             COMMAND asteria run "${source}" --max-steps ${ISA_MAX_STEPS})
    add_test(NAME roundtrip/${program}
             COMMAND "${CMAKE_COMMAND}" "-DASTERIA=$<TARGET_FILE:asteria>" "-DSOURCE=${source}"
                     "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/roundtrip"
                     -P "${PROJECT_SOURCE_DIR}/cmake/RoundTrip.cmake")
endforeach()

# The examples are held to the same standard.
foreach(example IN ITEMS sum)
    set(source "${PROJECT_SOURCE_DIR}/examples/${example}.asm")
    add_test(NAME examples/${example}
             COMMAND asteria run "${source}" --max-steps ${ISA_MAX_STEPS})
    add_test(NAME roundtrip/example_${example}
             COMMAND "${CMAKE_COMMAND}" "-DASTERIA=$<TARGET_FILE:asteria>" "-DSOURCE=${source}"
                     "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/roundtrip"
                     -P "${PROJECT_SOURCE_DIR}/cmake/RoundTrip.cmake")
endforeach()

# The mutation sweep: every check in every program must be able to fail (mutation_test.cpp).
asteria_add_test(conformance_mutations
    SOURCES mutation_test.cpp
    DEPS    machine toolchain_as)

# The source path and the program list go through a generated header: a compile definition would
# need the path escaped for every generator and shell (it may contain '#', spaces, ...), and one
# list here means a program can't join the suite without also being mutation-tested.
list(JOIN ISA_PROGRAMS "\", \"" program_list)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/isa_dir.hpp"
     CONTENT "#pragma once\ninline constexpr const char* kIsaDir = R\"path(${CMAKE_CURRENT_SOURCE_DIR})path\";\ninline constexpr const char* kIsaPrograms[] = {\"${program_list}\"};\n")
target_include_directories(test_conformance_mutations PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
