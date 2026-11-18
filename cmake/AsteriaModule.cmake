# Helpers that give every module and test target the same shape.
# See docs/architecture/OVERVIEW.md for the module rules these encode.

# asteria_add_module(<name> [SOURCES <files...>] [DEPS <modules...>])
#
#   Creates the library target asteria::<name> for src/<name>/.
#   - With SOURCES: a static library. Without: header-only (INTERFACE).
#   - DEPS are module names (e.g. "isa common"), not target names. They must match the
#     allowed-dependency table in OVERVIEW.md section 2. The 'architecture/layering'
#     test enforces that table on the #include lines themselves.
#   - Include paths start at src/, so code writes #include "mem/cache/cache.hpp".
function(asteria_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;DEPS")
    list(TRANSFORM ARG_DEPS PREPEND "asteria::" OUTPUT_VARIABLE dep_targets)

    set(target asteria_${name})
    if(ARG_SOURCES)
        add_library(${target} STATIC ${ARG_SOURCES})
        set(scope PUBLIC)
    else()
        add_library(${target} INTERFACE)
        set(scope INTERFACE)
    endif()

    target_include_directories(${target} ${scope} "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${target} ${scope} asteria::options ${dep_targets})
    add_library(asteria::${name} ALIAS ${target})
endfunction()

# asteria_add_test(<name> SOURCES <files...> DEPS <modules...>)
#
#   Creates the test executable test_<name> and registers each Catch2 TEST_CASE
#   with CTest as "<name>/<test case name>".
function(asteria_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;DEPS")
    list(TRANSFORM ARG_DEPS PREPEND "asteria::" OUTPUT_VARIABLE dep_targets)

    add_executable(test_${name} ${ARG_SOURCES})
    target_link_libraries(test_${name} PRIVATE ${dep_targets} Catch2::Catch2WithMain)
    catch_discover_tests(test_${name} TEST_PREFIX "${name}/")
endfunction()
