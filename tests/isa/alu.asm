; Conformance: register-register ALU (spec 4.1) and the hardwired R0.
;   ADD SUB AND OR XOR NOR SLL SRL SRA SLT SLTU
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .text
        .global main
main:
        LI      s0, 0x12345678
        LI      s1, 0x0F0F00FF
        LI      s2, -1                  ; 0xFFFFFFFF
        LI      s3, 0x80000000

        LI      a0, 1                   ; ADD
        ADD     t0, s0, s1
        LI      t1, 0x21435777
        BNE     t0, t1, fail

        LI      a0, 2                   ; ADD wraps modulo 2^32
        ADD     t0, s3, s3
        BNEZ    t0, fail
        ADD     t0, s2, s2
        LI      t1, 0xFFFFFFFE
        BNE     t0, t1, fail

        LI      a0, 3                   ; SUB
        SUB     t0, s1, s0
        LI      t1, 0xFCDAAA87
        BNE     t0, t1, fail
        SUB     t0, zero, s3            ; -INT_MIN wraps to itself
        BNE     t0, s3, fail

        LI      a0, 4                   ; AND
        AND     t0, s0, s1
        LI      t1, 0x02040078
        BNE     t0, t1, fail

        LI      a0, 5                   ; OR
        OR      t0, s0, s1
        LI      t1, 0x1F3F56FF
        BNE     t0, t1, fail

        LI      a0, 6                   ; XOR
        XOR     t0, s0, s1
        LI      t1, 0x1D3B5687
        BNE     t0, t1, fail

        LI      a0, 7                   ; NOR
        NOR     t0, s0, s1
        LI      t1, 0xE0C0A900
        BNE     t0, t1, fail
        NOR     t0, zero, zero          ; NOT 0
        BNE     t0, s2, fail

        LI      a0, 8                   ; SLL uses only rs2[4:0]: 36 shifts by 4
        LI      t2, 36
        SLL     t0, s0, t2
        LI      t1, 0x23456780
        BNE     t0, t1, fail
        LI      t2, 31
        SLL     t0, s2, t2
        BNE     t0, s3, fail

        LI      a0, 9                   ; SRL fills with zeros
        SRL     t0, s3, t2              ; t2 = 31
        LI      t1, 1
        BNE     t0, t1, fail
        LI      t2, 4
        SRL     t0, s2, t2
        LI      t1, 0x0FFFFFFF
        BNE     t0, t1, fail

        LI      a0, 10                  ; SRA fills with the sign bit
        SRA     t0, s3, t2              ; t2 = 4
        LI      t1, 0xF8000000
        BNE     t0, t1, fail
        SRA     t0, s0, t2
        LI      t1, 0x01234567
        BNE     t0, t1, fail
        LI      t2, 32                  ; 32 & 31 = 0: no shift
        SRA     t0, s3, t2
        BNE     t0, s3, fail

        LI      a0, 11                  ; SLT is signed
        SLT     t0, s3, s0              ; INT_MIN < positive
        LI      t1, 1
        BNE     t0, t1, fail
        SLT     t0, s0, s3
        BNEZ    t0, fail
        SLT     t0, s2, zero            ; -1 < 0
        BNE     t0, t1, fail
        SLT     t0, s0, s0
        BNEZ    t0, fail

        LI      a0, 12                  ; SLTU is unsigned
        SLTU    t0, s3, s0              ; 0x80000000 < 0x12345678? no
        BNEZ    t0, fail
        SLTU    t0, s0, s3
        BNE     t0, t1, fail            ; t1 = 1
        SLTU    t0, zero, s2
        BNE     t0, t1, fail

        LI      a0, 13                  ; writes to R0 are discarded
        ADD     zero, s0, s1
        BNEZ    zero, fail
        OR      t0, zero, zero
        BNEZ    t0, fail

        LI      a0, 14                  ; rd, rs1 and rs2 may be the same register
        LI      t0, 3
        ADD     t0, t0, t0
        LI      t1, 6
        BNE     t0, t1, fail
        SUB     t0, t0, t0
        BNEZ    t0, fail

        LI      a0, 0
fail:   HALT
