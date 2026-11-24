; sum.asm: adds up a table of numbers with a function call, stores the result, and
; halts with a0 = 0 if the result is right. Try:
;
;     asteria run examples/sum.asm
;     asteria run examples/sum.asm --trace
;     asteria build examples/sum.asm      and then      asteria disasm examples/sum.ast

        .equ    COUNT, 5

        .text
        .global main
main:
        LA      a0, numbers             ; a0 = address of the table
        LI      a1, COUNT               ; a1 = how many
        CALL    sum                     ; v0 = sum(a0, a1)
        LA      t0, result
        SW      v0, 0(t0)               ; result = v0

        LI      t1, 150                 ; the expected answer
        SUB     a0, v0, t1              ; a0 = 0 exactly when they match
        HALT

; sum(a0 = address, a1 = count) -> v0. A leaf function: no stack needed.
sum:
        LI      v0, 0
.Lloop:
        BEQZ    a1, .Ldone
        LW      t0, 0(a0)
        ADD     v0, v0, t0
        ADDI    a0, a0, 4
        ADDI    a1, a1, -1
        J       .Lloop
.Ldone:
        RET

        .rodata
numbers:
        .word   10, 20, 30, 40, 50

        .bss
result:
        .space  4
