; Conformance: conditional branches (spec 4.5), taken and not taken, signed and unsigned.
;   BEQ BNE BLT BGE BLTU BGEU
; A taken branch that should not be taken lands on `fail`; a branch that should be taken
; but falls through runs into `J fail`.
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .text
        .global main
main:
        LI      s1, 1
        LI      s2, -1                  ; 0xFFFFFFFF: signed -1, unsigned huge
        LI      s3, 0x80000000          ; signed min, unsigned 2^31
        LI      s4, 1                   ; equal to s1, but a different register: a branch
                                        ; must compare VALUES, not register numbers

        LI      a0, 1                   ; BEQ
        BEQ     s1, s2, fail
        BEQ     s1, s4, .L1
        J       fail
.L1:
        LI      a0, 2                   ; BNE
        BNE     s1, s4, fail
        BNE     s1, s2, .L2
        J       fail
.L2:
        LI      a0, 3                   ; BLT is signed: -1 < 1, INT_MIN < -1
        BLT     s1, s2, fail
        BLT     s1, s1, fail
        BLT     s2, s1, .L3a
        J       fail
.L3a:   BLT     s3, s2, .L3
        J       fail
.L3:
        LI      a0, 4                   ; BGE is signed; equal counts as >=
        BGE     s2, s1, fail
        BGE     s1, s2, .L4a
        J       fail
.L4a:   BGE     s1, s1, .L4
        J       fail
.L4:
        LI      a0, 5                   ; BLTU is unsigned: 1 < 0xFFFFFFFF
        BLTU    s2, s1, fail
        BLTU    s1, s1, fail
        BLTU    s1, s2, .L5a
        J       fail
.L5a:   BLTU    s3, s2, .L5
        J       fail
.L5:
        LI      a0, 6                   ; BGEU is unsigned
        BGEU    s1, s2, fail
        BGEU    s2, s1, .L6a
        J       fail
.L6a:   BGEU    s3, s3, .L6
        J       fail
.L6:
        LI      a0, 7                   ; backward branches: a loop runs exactly 5 times
        LI      t0, 5
        LI      t1, 0
.Lloop: ADDI    t1, t1, 1
        ADDI    t0, t0, -1
        BNEZ    t0, .Lloop
        LI      t2, 5
        BNE     t1, t2, fail

        LI      a0, 8                   ; relative targets count instructions: +2 skips one
        BEQ     zero, zero, +2
        J       fail
        BNE     zero, zero, +2          ; not taken: falls through to the next line
        BEQ     zero, zero, +2
        J       fail

        LI      a0, 9                   ; pseudo-branches swap their operands correctly
        BGT     s1, s2, .L9a            ; 1 > -1
        J       fail
.L9a:   BLE     s1, s2, fail
        BGTU    s1, s2, fail            ; 1 > 0xFFFFFFFF unsigned? no
        BLEU    s1, s2, .L9
        J       fail
.L9:
        LI      a0, 0
fail:   HALT
