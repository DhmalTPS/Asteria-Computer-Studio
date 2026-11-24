# machine: wires concrete components into a computer (M1: physical bus + reference interpreter).
asteria_add_module(machine
    SOURCES machine.cpp
    DEPS    cpu_interp mem_phys toolchain_axf isa common)
