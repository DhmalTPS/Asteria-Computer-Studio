# mem/phys: the physical bus - Boot ROM, RAM and the MMIO window, routed by address (D12).
asteria_add_module(mem_phys
    SOURCES physical_bus.cpp
    DEPS    sim common)
