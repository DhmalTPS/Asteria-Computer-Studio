# cpu/core: the parts every CPU model shares (D23) - register file, ALU, CSR file with
# trap entry, and the instruction semantics written against the ExecContext interface.
asteria_add_module(cpu_core
    SOURCES csr_file.cpp semantics.cpp
    DEPS    isa common)
