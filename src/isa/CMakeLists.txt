# isa: the ASTERIA-32 instruction set as data - the instruction table, encoder, decoder,
# disassembler, register names, CSR and exception definitions. Pure functions, no state.
asteria_add_module(isa
    SOURCES instruction_set.cpp encoding.cpp registers.cpp csrs.cpp disassemble.cpp
    DEPS    common)
