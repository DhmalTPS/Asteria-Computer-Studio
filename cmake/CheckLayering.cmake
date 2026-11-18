# Enforces the module dependency rules (decision D18, OVERVIEW.md section 2).
#
# Run as a CTest test (architecture/layering), or by hand:
#   cmake -DSOURCE_DIR=<repo>/src -P cmake/CheckLayering.cmake
#
# Why a script and not just CMake target links? All modules share the include root src/,
# so a file could #include another module's header-only code without linking to it.
# Checking the #include lines themselves closes that loophole.

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "Pass -DSOURCE_DIR=<repo>/src")
endif()

# Allowed dependencies per module. Keep in sync with OVERVIEW.md section 2.
set(allowed_common    "")
set(allowed_isa       "common")
set(allowed_sim       "common")
set(allowed_cpu       "isa;sim;common")
set(allowed_mem       "sim;common")
set(allowed_io        "sim;common")
set(allowed_toolchain "isa;common")
set(allowed_machine   "cpu;mem;io;toolchain;sim;isa;common")
set(allowed_studio    "machine;cpu;mem;io;toolchain;sim;isa;common")
set(modules common isa sim cpu mem io toolchain machine studio)

file(GLOB_RECURSE files "${SOURCE_DIR}/*.hpp" "${SOURCE_DIR}/*.cpp")

set(violations 0)
foreach(file IN LISTS files)
    file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
    string(REGEX MATCH "^[^/]+" module "${rel}")

    if(NOT module IN_LIST modules)
        message(SEND_ERROR "${rel}: '${module}' is not a known module (add it to OVERVIEW.md and this script)")
        math(EXPR violations "${violations} + 1")
        continue()
    endif()

    file(STRINGS "${file}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS include_lines)
        if(NOT line MATCHES "\"([^/\"]+)/")
            continue()  # same-directory include such as "foo.hpp"
        endif()
        set(dependency "${CMAKE_MATCH_1}")
        if(dependency IN_LIST modules
           AND NOT dependency STREQUAL module
           AND NOT dependency IN_LIST allowed_${module})
            message(SEND_ERROR "${rel}: module '${module}' must not depend on '${dependency}' -> ${line}")
            math(EXPR violations "${violations} + 1")
        endif()
    endforeach()
endforeach()

list(LENGTH files file_count)
if(violations GREATER 0)
    message(FATAL_ERROR "Layering check failed: ${violations} violation(s)")
endif()
message(STATUS "Layering check passed: ${file_count} file(s), 0 violations")
