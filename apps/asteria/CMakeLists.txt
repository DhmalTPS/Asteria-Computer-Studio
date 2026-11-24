# asteria: the command-line front end (build | run | debug | disasm | profile | bench).
add_executable(asteria main.cpp)
target_link_libraries(asteria PRIVATE asteria::isa)
target_compile_definitions(asteria PRIVATE ASTERIA_VERSION="${PROJECT_VERSION}")
