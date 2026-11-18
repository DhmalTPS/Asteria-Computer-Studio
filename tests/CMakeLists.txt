find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)  # catch_discover_tests()

add_subdirectory(unit)

# Architecture rules: no module may #include a module it is not allowed to depend on (D18).
add_test(NAME architecture/layering
         COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src"
                 -P "${PROJECT_SOURCE_DIR}/cmake/CheckLayering.cmake")
