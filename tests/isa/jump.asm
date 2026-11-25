; Conformance: JAL and JALR (spec 4.6), calls, returns and the stack.
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .text
        .global main
main:
        LI      a0, 1                   ; JAL links PC + 4 and jumps
j1:     JAL     t0, .L1
        J       fail
.L1:    LA      t1, j1
        ADDI    t1, t1, 4
        BNE     t0, t1, fail

        LI      a0, 2                   ; J (JAL R0) jumps without linking
        J       .L2
        J       fail
.L2:
        LI      a0, 3                   ; JALR to a register
        LA      t0, .L3
j3:     JALR    t1, t0, 0
        J       fail
.L3:    LA      t2, j3
        ADDI    t2, t2, 4
        BNE     t1, t2, fail

        LI      a0, 4                   ; JALR adds a signed offset
        LA      t0, .L4 - 8
        JALR    t1, t0, 8
        J       fail
.L4:    LA      t0, .L4b + 12
        JALR    t1, t0, -12
        J       fail
.L4b:
        LI      a0, 5                   ; JALR with rd = rs1: the target uses the old value
        LA      t0, .L5
j5:     JALR    t0, t0, 0
        J       fail
.L5:    LA      t2, j5
        ADDI    t2, t2, 4
        BNE     t0, t2, fail

        LI      a0, 6                   ; nested calls through the stack
        LI      a1, 5
        CALL    factorial
        LI      t1, 120
        BNE     v0, t1, fail

        LI      a0, 7                   ; the stack pointer is back where it started
        LA      t0, initial_sp
        LW      t0, 0(t0)
        BNEZ    t0, .L7
        J       fail
.L7:    BNE     sp, t0, fail

        LI      a0, 8                   ; JAL backward: a negative offset
        J       .L8b
.L8a:   LA      t1, j8
        ADDI    t1, t1, 4
        BNE     t0, t1, fail
        J       .L8c
.L8b:
j8:     JAL     t0, .L8a
        J       fail
.L8c:
        LI      a0, 0
fail:   HALT

; factorial(a1) -> v0, recursive, saving ra and a1 on the stack.
factorial:
        LA      t0, initial_sp          ; remember the first sp seen
        LW      t1, 0(t0)
        BNEZ    t1, .Lsaved
        ADDI    t1, sp, 0
        SW      t1, 0(t0)
.Lsaved:
        BNEZ    a1, .Lrecurse
        LI      v0, 1
        RET
.Lrecurse:
        ADDI    sp, sp, -8
        SW      ra, 4(sp)
        SW      a1, 0(sp)
        ADDI    a1, a1, -1
        CALL    factorial
        LW      a1, 0(sp)
        LW      ra, 4(sp)
        ADDI    sp, sp, 8
        MUL     v0, v0, a1
        RET

        .bss
initial_sp:
        .space  4
