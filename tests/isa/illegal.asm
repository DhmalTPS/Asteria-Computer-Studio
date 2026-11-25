; Conformance: strict decoding (spec 2, 3, 4.3, 4.7, 4.8). Every word in the table `bad` must raise
; IllegalInstruction: reserved opcodes, undefined funct11 values, non-zero unused fields, and CSR
; numbers that don't exist. A decoder that ignores a field (D3) would run some of these as real
; instructions, and the differential fuzzer (M3) could no longer trust that two CPUs "agree".
;
; How: each word is stored into `probe`, made visible to fetch with FENCE.I, and called. The handler
; logs (CAUSE, EPC) and returns past the probe to its RET. Check 1 first proves the probe really
; runs what was stored, so the "must trap" checks can't pass by accident.
;
; Every word was derived by hand from the field layout in spec 2:
;   funct11 [31:21] | rs2 [20:16] | rs1 [15:11] | rd [10:6] | opcode [5:0]
;
; Passes by halting with a0 = 0. On failure a0 = the failed check: 1 for the probe, 2 + i for
; table word i (0-based).

        .equ    ILLEGAL, 3

        .text
        .global main
main:
        LA      t0, handler
        CSRW    TVEC, t0
        LA      s4, log
        LA      s0, probe

        LI      a0, 1                   ; the probe runs a stored legal word, without a trap
        LI      t0, 0x002A0144          ; ADDI a1, zero, 42
        SW      t0, 0(s0)
        FENCE.I
        MOV     v0, s4
        CALL    probe
        BNE     s4, v0, fail
        LI      t0, 42
        BNE     a1, t0, fail

        LA      s1, bad                 ; each table word: exactly one trap, Illegal, at the probe
        LA      s2, bad_end
        LI      a0, 2
next:   BEQ     s1, s2, done
        LW      t0, 0(s1)
        SW      t0, 0(s0)
        FENCE.I
        MOV     v0, s4
        CALL    probe
        ADDI    v0, v0, 8
        BNE     s4, v0, fail
        LW      t0, -8(s4)
        LI      t1, ILLEGAL
        BNE     t0, t1, fail
        LW      t0, -4(s4)
        BNE     t0, s0, fail
        ADDI    s1, s1, 4
        ADDI    a0, a0, 1
        J       next

done:   LI      a0, 0
fail:   HALT

probe:  NOP                             ; overwritten before every call
        RET

; ---- the trap handler: log (CAUSE, EPC), resume after the trapping instruction ----------------
handler:
        CSRR    t8, CAUSE
        SW      t8, 0(s4)
        CSRR    t9, EPC
        SW      t9, 4(s4)
        ADDI    s4, s4, 8
        ADDI    t9, t9, 4
        CSRW    EPC, t9
        ERET

        .rodata
bad:
        ; The zero word, and every reserved opcode (spec 3) with all other fields 0.
        .word   0x00000000
        .word   0x00000003, 0x0000000F, 0x00000013, 0x00000016, 0x00000017, 0x0000001B
        .word   0x0000001D, 0x0000001E, 0x0000001F, 0x00000026, 0x00000027
        .word   0x0000002A, 0x0000002B, 0x0000002C, 0x0000002D, 0x0000002E, 0x0000002F
        .word   0x00000037, 0x00000038, 0x00000039, 0x0000003A, 0x0000003B, 0x0000003C
        .word   0x0000003D, 0x0000003E, 0x0000003F

        ; Undefined funct11 values: the first one past each group's table, and the largest.
        .word   0x01600001              ; OP      funct11 = 11
        .word   0xFFE00001              ; OP      funct11 = 0x7FF
        .word   0x00E00002              ; MULDIV  funct11 = 7
        .word   0xFFE00002              ; MULDIV  funct11 = 0x7FF
        .word   0x0020001C              ; AMO     funct11 = 1
        .word   0x00E00030              ; SYSTEM  funct11 = 7

        ; SYSTEM instructions with a field that must be 0 (spec 4.7).
        .word   0x00000070              ; ECALL     rd = 1
        .word   0x00000830              ; ECALL     rs1 = 1
        .word   0x00010030              ; ECALL     rs2 = 1
        .word   0x002007F0              ; EBREAK    rd = 31
        .word   0x00400070              ; ERET      rd = 1
        .word   0x00600830              ; WFI       rs1 = 1
        .word   0x009F0030              ; HALT      rs2 = 31
        .word   0x00A00070              ; TLBFLUSH  rd = 1 (it uses rs1 and rs2, never rd)
        .word   0x00C00830              ; FENCE.I   rs1 = 1

        ; I-type rules (spec 4.3).
        .word   0x0020000A              ; SLLI  imm = 0x0020: imm[15:5] != 0
        .word   0x0400000B              ; SRLI  imm = 0x0400
        .word   0x8000000C              ; SRAI  imm = 0x8000 (the sign bit)
        .word   0x0000F80D              ; LUI   rs1 = 31
        .word   0x0000080E              ; AUIPC rs1 = 1

        ; CSR numbers (spec 4.8, 7). imm[15:12] != 0 is illegal even where the low 12 bits name a
        ; real CSR: 0x1000 must not alias STATUS. Then numbers just past the defined ones.
        .word   0x10000032              ; CSRRS  zero, 0x1000, zero
        .word   0x80000034              ; CSRRWI zero, 0x8000, 0
        .word   0x00090032              ; CSRRS  zero, 0x009, zero   (after IPEND)
        .word   0x00140032              ; CSRRS  zero, 0x014, zero   (after INSTRETH)
        .word   0x00F10032              ; CSRRS  zero, 0x0F1, zero   (after CPUID)
        .word   0x0FFF0032              ; CSRRS  zero, 0xFFF, zero
bad_end:

        .bss
log:    .space  2048
