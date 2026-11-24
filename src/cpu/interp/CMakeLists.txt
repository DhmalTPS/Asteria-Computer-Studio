# cpu/interp: the reference interpreter - the correctness oracle for every CPU model (spec §10).
asteria_add_module(cpu_interp
    SOURCES interpreter.cpp page_walk.cpp
    DEPS    cpu_core sim isa common)
