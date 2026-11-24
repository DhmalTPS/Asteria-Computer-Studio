# sim: the contracts that components meet through (OVERVIEW.md section 3).
# M1 needs only the untimed memory contract; the clocked contracts arrive with M2.
asteria_add_module(sim
    DEPS common)
