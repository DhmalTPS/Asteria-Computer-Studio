; A misaligned load with no handler: TVEC = 0 sends it into a trap loop (exit code 3).
        LI      t0, 2
        LW      a0, 0(t0)
        HALT
