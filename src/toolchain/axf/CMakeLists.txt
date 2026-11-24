# toolchain/axf: read and write AXF containers (D17, docs/formats/AXF.md).
asteria_add_module(toolchain_axf
    SOURCES axf.cpp
    DEPS    common)
