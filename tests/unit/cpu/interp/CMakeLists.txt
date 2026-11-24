# The interpreter tests run programs on a real PhysicalBus, so ROM, RAM and MMIO rules apply.
asteria_add_test(cpu_interp
    SOURCES interpreter_test.cpp paging_test.cpp
    DEPS    cpu_interp mem_phys)
