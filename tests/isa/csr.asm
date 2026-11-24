; Conformance: CSR instructions and registers (spec 4.8, 7), in Kernel mode.
;   CSRRW CSRRS CSRRC CSRRWI CSRRSI CSRRCI; reset values, writable masks, counters.
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .text
        .global main
main:
        LI      a0, 1                   ; reset values (nothing has touched them yet)
        CSRR    t0, STATUS              ; Kernel mode, interrupts off
        LI      t1, 0x2
        BNE     t0, t1, fail
        CSRR    t0, TVEC
        BNEZ    t0, fail
        CSRR    t0, PTBR
        BNEZ    t0, fail
        CSRR    t0, IMASK
        BNEZ    t0, fail
        CSRR    t0, IPEND               ; no devices: no lines
        BNEZ    t0, fail

        LI      a0, 2                   ; CPUID: ISA version 1.0
        CSRR    t0, CPUID
        LI      t1, 0x00010000
        BNE     t0, t1, fail

        LI      a0, 3                   ; CSRRW returns the old value
        LI      s0, 0x12345678
        LI      s1, 0x9ABCDEF0
        CSRW    SCRATCH, s0
        CSRRW   t0, SCRATCH, s1
        BNE     t0, s0, fail
        CSRR    t0, SCRATCH
        BNE     t0, s1, fail

        LI      a0, 4                   ; CSRRS sets bits, CSRRC clears them
        LI      t0, 0xF0
        CSRW    SCRATCH, t0
        LI      t1, 0x0F
        CSRRS   t2, SCRATCH, t1
        BNE     t2, t0, fail            ; old value
        CSRR    t2, SCRATCH
        LI      t3, 0xFF
        BNE     t2, t3, fail
        LI      t1, 0x3C
        CSRRC   t2, SCRATCH, t1
        BNE     t2, t3, fail
        CSRR    t2, SCRATCH
        LI      t3, 0xC3
        BNE     t2, t3, fail

        LI      a0, 5                   ; the immediate forms use a 5-bit unsigned operand
        CSRRWI  t2, SCRATCH, 31
        BNE     t2, t3, fail            ; old 0xC3
        CSRRSI  t2, SCRATCH, 0          ; operand 0: no write
        LI      t3, 31
        BNE     t2, t3, fail
        CSRRCI  t2, SCRATCH, 1
        CSRR    t2, SCRATCH
        LI      t3, 30
        BNE     t2, t3, fail
        CSRRSI  t2, SCRATCH, 1
        CSRR    t2, SCRATCH
        LI      t3, 31
        BNE     t2, t3, fail

        LI      a0, 6                   ; CSR numbers work as well as names
        CSRR    t0, 0x005
        LI      t1, 31
        BNE     t0, t1, fail

        LI      a0, 7                   ; writable masks: undefined bits read as 0
        LI      s2, -1
        CSRW    EPC, s2                 ; bits [1:0] read as 0
        CSRR    t0, EPC
        LI      t1, 0xFFFFFFFC
        BNE     t0, t1, fail
        CSRW    TVEC, s2                ; bit 1 reads as 0
        CSRR    t0, TVEC
        LI      t1, 0xFFFFFFFD
        BNE     t0, t1, fail
        CSRW    TVEC, zero
        CSRW    CAUSE, s2               ; [31], [15:8], [7:0]
        CSRR    t0, CAUSE
        LI      t1, 0x8000FFFF
        BNE     t0, t1, fail
        CSRW    BADVADDR, s2
        CSRR    t0, BADVADDR
        BNE     t0, s2, fail
        CSRW    IMASK, s2               ; 8 lines
        CSRR    t0, IMASK
        LI      t1, 0xFF
        BNE     t0, t1, fail
        CSRW    IMASK, zero
        LI      t2, 0xFFFFFFFE          ; everything but EN: paging stays off
        CSRW    PTBR, t2
        CSRR    t0, PTBR
        LI      t1, 0xFFFFFFF0
        BNE     t0, t1, fail
        CSRW    PTBR, zero
        LI      t2, 0xFFFFFFF3          ; IE and KM stay 1, PIE and PKM 0, the rest undefined
        CSRW    STATUS, t2
        CSRR    t0, STATUS
        LI      t1, 0x3
        BNE     t0, t1, fail
        CSRRCI  zero, STATUS, 1         ; interrupts off again; stay in Kernel mode
        CSRR    t0, STATUS
        LI      t1, 0x2
        BNE     t0, t1, fail

        LI      a0, 8                   ; INSTRET: a read sees the count before itself
        CSRR    t0, INSTRET
        NOP
        NOP
        CSRR    t1, INSTRET             ; CSRR, NOP, NOP completed in between
        SUB     t1, t1, t0
        LI      t2, 3
        BNE     t1, t2, fail
        CSRR    t0, INSTRETH            ; far from 2^32 instructions
        BNEZ    t0, fail

        LI      a0, 9                   ; CYCLE never runs slower than one per instruction
        CSRR    t0, CYCLE
        NOP
        NOP
        CSRR    t1, CYCLE
        SUB     t1, t1, t0
        LI      t2, 3
        BLTU    t1, t2, fail
        CSRR    t0, CYCLEH
        BNEZ    t0, fail

        LI      a0, 0
fail:   HALT
