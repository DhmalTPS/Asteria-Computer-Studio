; Conformance: immediate ALU (spec 4.3).
;   ADDI SLTI SLTIU ANDI ORI XORI SLLI SRLI SRAI LUI AUIPC
; Arithmetic immediates are sign-extended; logical ones are zero-extended.
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .text
        .global main
main:
        LI      s0, 0x12345678
        LI      s2, -1                  ; 0xFFFFFFFF
        LI      s3, 0x80000000
        LI      t1, 1

        LI      a0, 1                   ; ADDI sign-extends
        ADDI    t0, zero, -1
        BNE     t0, s2, fail
        ADDI    t0, s0, 0x7FFF
        LI      t2, 0x1234D677
        BNE     t0, t2, fail
        ADDI    t0, s0, -32768
        LI      t2, 0x1233D678
        BNE     t0, t2, fail

        LI      a0, 2                   ; SLTI is signed
        SLTI    t0, s3, -1
        BNE     t0, t1, fail
        SLTI    t0, zero, -1
        BNEZ    t0, fail

        LI      a0, 3                   ; SLTIU: sign-extend, then compare unsigned
        SLTIU   t0, s0, -1              ; 0x12345678 < 0xFFFFFFFF
        BNE     t0, t1, fail
        SLTIU   t0, s2, -1              ; equal: not less
        BNEZ    t0, fail
        SLTIU   t0, zero, 1             ; SEQZ
        BNE     t0, t1, fail

        LI      a0, 4                   ; ANDI zero-extends
        ANDI    t0, s2, 0x8000
        LI      t2, 0x00008000
        BNE     t0, t2, fail

        LI      a0, 5                   ; ORI zero-extends
        ORI     t0, zero, 0xFFFF
        LI      t2, 0x0000FFFF
        BNE     t0, t2, fail

        LI      a0, 6                   ; XORI zero-extends
        XORI    t0, s2, 0xFFFF
        LI      t2, 0xFFFF0000
        BNE     t0, t2, fail

        LI      a0, 7                   ; shifts by an immediate
        SLLI    t0, s0, 8
        LI      t2, 0x34567800
        BNE     t0, t2, fail
        SRLI    t0, s3, 31
        BNE     t0, t1, fail
        SRAI    t0, s3, 31
        BNE     t0, s2, fail
        SRAI    t0, s0, 0
        BNE     t0, s0, fail

        LI      a0, 8                   ; LUI fills the upper half, zeros the lower
        LUI     t0, 0xFFFF
        LI      t2, 0xFFFF0000
        BNE     t0, t2, fail
        LUI     t0, 0
        BNEZ    t0, fail

        LI      a0, 9                   ; AUIPC adds to its own address
here:   AUIPC   t0, 1
        LA      t2, here
        LUI     t3, 1
        ADD     t2, t2, t3
        BNE     t0, t2, fail
there:  AUIPC   t0, 0
        LA      t2, there
        BNE     t0, t2, fail

        LI      a0, 10                  ; LUI + ORI builds any constant; %hi_adj pairs with ADDI
        .equ    X, 0x9ABC8765
        LUI     t0, %hi(X)
        ORI     t0, t0, %lo(X)
        LI      t2, X
        BNE     t0, t2, fail
        LUI     t0, %hi_adj(X)
        ADDI    t0, t0, %lo(X)
        BNE     t0, t2, fail

        LI      a0, 0
fail:   HALT
