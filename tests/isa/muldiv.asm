; Conformance: multiply and divide (spec 4.2), including every special case.
;   MUL MULH MULHU DIV DIVU REM REMU
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .text
        .global main
main:
        LI      s0, 0x12345678
        LI      s2, -1                  ; 0xFFFFFFFF
        LI      s3, 0x80000000
        LI      s4, 7
        LI      s5, -7
        LI      s6, 2
        LI      s7, -2

        LI      a0, 1                   ; MUL keeps the low 32 bits
        LI      t2, -3
        MUL     t0, s4, t2
        LI      t1, 0xFFFFFFEB          ; -21
        BNE     t0, t1, fail
        LI      t2, 0x10
        MUL     t0, s0, t2
        LI      t1, 0x23456780
        BNE     t0, t1, fail

        LI      a0, 2                   ; MULH: signed x signed, high half
        MULH    t0, s3, s3              ; 2^62
        LI      t1, 0x40000000
        BNE     t0, t1, fail
        MULH    t0, s2, s2              ; (-1)(-1) = 1
        BNEZ    t0, fail
        LI      t2, 1
        MULH    t0, s2, t2              ; -1: high half all ones
        BNE     t0, s2, fail

        LI      a0, 3                   ; MULHU: unsigned x unsigned, high half
        MULHU   t0, s2, s2              ; 0xFFFFFFFE_00000001
        LI      t1, 0xFFFFFFFE
        BNE     t0, t1, fail
        MULHU   t0, s3, s6              ; 2^32
        LI      t1, 1
        BNE     t0, t1, fail

        LI      a0, 4                   ; DIV and REM round toward zero
        DIV     t0, s5, s6              ; -7 / 2 = -3
        LI      t1, -3
        BNE     t0, t1, fail
        REM     t0, s5, s6              ; -7 % 2 = -1
        BNE     t0, s2, fail
        DIV     t0, s4, s7              ; 7 / -2 = -3
        BNE     t0, t1, fail
        REM     t0, s4, s7              ; 7 % -2 = 1
        LI      t1, 1
        BNE     t0, t1, fail

        LI      a0, 5                   ; DIVU and REMU are unsigned
        DIVU    t0, s7, s6              ; 0xFFFFFFFE / 2
        LI      t1, 0x7FFFFFFF
        BNE     t0, t1, fail
        LI      t2, 0x10
        REMU    t0, s2, t2
        LI      t1, 0xF
        BNE     t0, t1, fail

        LI      a0, 6                   ; division by zero: quotient all ones, remainder = dividend
        DIV     t0, s0, zero
        BNE     t0, s2, fail
        DIVU    t0, s0, zero
        BNE     t0, s2, fail
        REM     t0, s0, zero
        BNE     t0, s0, fail
        REMU    t0, s5, zero
        BNE     t0, s5, fail

        LI      a0, 7                   ; the one signed overflow: INT_MIN / -1
        DIV     t0, s3, s2
        BNE     t0, s3, fail
        REM     t0, s3, s2
        BNEZ    t0, fail

        LI      a0, 0
fail:   HALT
