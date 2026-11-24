# toolchain/as: the assembler (spec section 11) and the reassemblable listing.
asteria_add_module(toolchain_as
    SOURCES lexer.cpp assembler.cpp listing.cpp
    DEPS    toolchain_axf isa common)
