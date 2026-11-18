# asteria::options - compile settings shared by every Asteria target (decision D20).
# Every module links it, so these flags reach the whole codebase from this one place.

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(asteria_options INTERFACE)
add_library(asteria::options ALIAS asteria_options)

target_compile_features(asteria_options INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(asteria_options INTERFACE
        /W4                 # high warning level
        /WX                 # warnings are errors
        /permissive-        # strict standard conformance
        /utf-8              # source and execution charset
        /Zc:__cplusplus     # report the real language version
        /Zc:preprocessor    # standard-conforming preprocessor
        /EHsc)              # standard C++ exception model
else()
    target_compile_options(asteria_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()
