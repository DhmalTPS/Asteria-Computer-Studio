; Conformance: exceptions, trap entry, ERET and privilege, with paging off (spec 4.7, 5, 6).
;
; The handler logs every trap as four words (CAUSE, EPC, BADVADDR, STATUS at entry), then
; returns past the trapping instruction. Two cases return elsewhere:
;   - a failed fetch (InstrPageFault, InstrBusError) returns to ra, the caller of the bad jump;
;   - ECALL from User mode returns to the kernel at s5, staying in Kernel mode.
; Registers: s4 = log pointer, s5 = kernel continuation; t8 and t9 belong to the handler.
;
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .equ    ILLEGAL, 3
        .equ    PRIVILEGE, 4

        .text
        .global main
main:
        LA      t0, handler
        CSRW    TVEC, t0
        LA      s4, log

        LI      a0, 1                   ; ECALL: code 6, EPC = the ECALL, BADVADDR unchanged
        LI      t0, 0x1234
        CSRW    BADVADDR, t0
t1_at:  ECALL
        LW      t0, -16(s4)
        LI      t1, 6
        BNE     t0, t1, fail
        LW      t0, -12(s4)
        LA      t1, t1_at
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        LI      t1, 0x1234
        BNE     t0, t1, fail

        LI      a0, 2                   ; trap entry and ERET in Kernel mode
        LW      t0, -4(s4)              ; at entry: PIE = 0, PKM = 1, IE = 0, KM = 1
        LI      t1, 0xA
        BNE     t0, t1, fail
        CSRR    t0, STATUS              ; after ERET: IE <- PIE, KM <- PKM; PIE, PKM unchanged
        BNE     t0, t1, fail
        CSRRSI  zero, STATUS, 1         ; IE = 1 (no interrupt line can be raised in M1)
        ECALL
        LW      t0, -4(s4)              ; at entry: PIE = 1, IE = 0
        LI      t1, 0xE
        BNE     t0, t1, fail
        CSRR    t0, STATUS              ; ERET restored IE = 1
        LI      t1, 0xF
        BNE     t0, t1, fail
        CSRRCI  zero, STATUS, 1

        LI      a0, 3                   ; EBREAK: code 5
t3_at:  EBREAK
        LW      t0, -16(s4)
        LI      t1, 5
        BNE     t0, t1, fail
        LW      t0, -12(s4)
        LA      t1, t3_at
        BNE     t0, t1, fail

        LI      a0, 4                   ; illegal instructions: code 3
t4_at:  .word   0                       ; the zero word
        LW      t0, -16(s4)
        LI      t1, ILLEGAL
        BNE     t0, t1, fail
        LW      t0, -12(s4)
        LA      t3, t4_at
        BNE     t0, t3, fail
        .word   0x00000003              ; reserved opcode 3
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        .word   0x0000080D              ; LUI with a non-zero rs1 field
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        .word   0x01600001              ; OP group with an undefined funct11 (11)
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        LI      t0, 1
        CSRRS   t2, CPUID, t0           ; writing a read-only CSR (rs1 is not R0)
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        LI      t2, 0x55
        CSRR    t2, 0x7FF               ; an unknown CSR; rd is not written
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        LI      t0, 0x55
        BNE     t2, t0, fail

        LI      a0, 5                   ; misaligned loads: code 7, BADVADDR = EA, rd unchanged
        LA      s0, data
        LI      t2, 0x77
t5_at:  LW      t2, 2(s0)
        LW      t0, -16(s4)
        LI      t1, 7
        BNE     t0, t1, fail
        LW      t0, -12(s4)
        LA      t1, t5_at
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        ADDI    t1, s0, 2
        BNE     t0, t1, fail
        LI      t1, 0x77
        BNE     t2, t1, fail
        LH      t2, 1(s0)
        LW      t0, -16(s4)
        LI      t1, 7
        BNE     t0, t1, fail
        LB      t2, 1(s0)               ; bytes are never misaligned
        LI      t1, 0x22
        BNE     t2, t1, fail

        LI      a0, 6                   ; misaligned stores and SWAP: code 8, memory unchanged
        LI      t2, -1
        SW      t2, 1(s0)
        LW      t0, -16(s4)
        LI      t1, 8
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        ADDI    t3, s0, 1
        BNE     t0, t3, fail
        SH      t2, 3(s0)
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        ADDI    t3, s0, 2
        SWAP    t0, t2, (t3)
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        LW      t0, 0(s0)               ; nothing was written
        LI      t1, 0x44332211
        BNE     t0, t1, fail

        LI      a0, 7                   ; JALR to a misaligned target: code 0, rd not written
        LA      t0, t7_target
        LI      ra, 0x99
t7_at:  JALR    ra, t0, 2
        LW      t0, -16(s4)
        BNEZ    t0, fail
        LW      t0, -12(s4)
        LA      t1, t7_at
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        LA      t1, t7_target
        ADDI    t1, t1, 2
        BNE     t0, t1, fail
        LI      t1, 0x99
        BNE     ra, t1, fail
t7_target:

        LI      a0, 8                   ; bus errors: unpopulated memory, stores to ROM, MMIO
        LI      s1, 0x40000000          ; nothing is there
        LW      t2, 0(s1)
        LW      t0, -16(s4)
        LI      t1, 11                  ; LoadBusError
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        BNE     t0, s1, fail
        SW      t2, 0(zero)             ; ROM is read-only
        LW      t0, -16(s4)
        LI      t1, 12                  ; StoreBusError
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        BNEZ    t0, fail
        LI      s2, 0x40000004
        SWAP    t2, t2, (s2)            ; SWAP fails as a store
        LW      t0, -16(s4)
        BNE     t0, t1, fail

        LI      a0, 9                   ; fetching from nowhere: code 2, EPC = BADVADDR = target
        JALR    ra, s1, 0               ; s1 = 0x40000000
        LW      t0, -16(s4)
        LI      t1, 2
        BNE     t0, t1, fail
        LW      t0, -12(s4)
        BNE     t0, s1, fail
        LW      t0, -8(s4)
        BNE     t0, s1, fail

        LI      a0, 10                  ; TVEC vectored mode: exceptions still go to the base
        LA      t0, handler
        ORI     t0, t0, 1
        CSRW    TVEC, t0
        ECALL
        LW      t0, -16(s4)
        LI      t1, 6
        BNE     t0, t1, fail
        LA      t0, handler
        CSRW    TVEC, t0

        LI      a0, 11                  ; User mode: K-only instructions and CSRs trap
        LA      s6, log_user            ; where the user part's traps start
        MOV     s4, s6
        LA      t0, user_code
        CSRW    EPC, t0
        CSRRCI  zero, STATUS, 12        ; PIE = PKM = 0: ERET enters User mode, IE = 0
        LA      s5, back_in_kernel
        ERET

back_in_kernel:
        CSRR    t0, STATUS              ; back in Kernel mode, through the ECALL
        ANDI    t0, t0, 2
        BEQZ    t0, fail
        SUB     t0, s4, s6              ; six traps were logged, 16 bytes each
        LI      t2, 96
        BNE     t0, t2, fail
        LW      t0, 0(s6)               ; HALT
        LI      t1, PRIVILEGE
        BNE     t0, t1, fail
        LW      t0, 4(s6)
        LA      t2, u_halt
        BNE     t0, t2, fail
        LW      t0, 12(s6)              ; STATUS at entry: KM = 1, PKM = 0 (came from User)
        LI      t2, 0x2
        BNE     t0, t2, fail
        LW      t0, 16(s6)              ; CSRR STATUS
        BNE     t0, t1, fail
        LW      t0, 32(s6)              ; unknown CSR: illegal wins over privilege
        LI      t2, ILLEGAL
        BNE     t0, t2, fail
        LW      t0, 48(s6)              ; ERET
        BNE     t0, t1, fail
        LW      t0, 64(s6)              ; WFI
        BNE     t0, t1, fail
        LW      t0, 80(s6)              ; ECALL from User mode
        LI      t2, 6
        BNE     t0, t2, fail
        LI      t2, 0x00010000          ; the counters and CPUID are readable in User mode
        BNE     s7, t2, fail

        LI      a0, 0
fail:   HALT

; ---- runs in User mode ----------------------------------------------------------------------
user_code:
u_halt: HALT                            ; PrivilegeViolation
        CSRR    t0, STATUS              ; PrivilegeViolation
        CSRR    t0, 0x7FF               ; IllegalInstruction
        CSRR    s7, CPUID               ; allowed
        CSRR    t0, INSTRET             ; allowed
        ERET                            ; PrivilegeViolation
        WFI                             ; PrivilegeViolation
        ECALL                           ; EnvironmentCall: the handler returns to the kernel

; ---- the trap handler -----------------------------------------------------------------------
handler:
        CSRR    t8, CAUSE
        SW      t8, 0(s4)
        CSRR    t9, EPC
        SW      t9, 4(s4)
        CSRR    t9, BADVADDR
        SW      t9, 8(s4)
        CSRR    t9, STATUS
        SW      t9, 12(s4)
        ADDI    s4, s4, 16
        ANDI    t9, t8, 0xFF            ; the code
        ADDI    t9, t9, -1
        SLTIU   t9, t9, 2               ; code 1 or 2: a failed fetch
        BNEZ    t9, .Lto_ra
        LI      t9, 6
        BNE     t8, t9, .Lnext
        CSRR    t9, STATUS
        ANDI    t9, t9, 8               ; PKM: did the ECALL come from Kernel mode?
        BNEZ    t9, .Lnext
        JR      s5                      ; from User mode: continue in the kernel
.Lnext: CSRR    t9, EPC
        ADDI    t9, t9, 4
        CSRW    EPC, t9
        ERET
.Lto_ra:
        CSRW    EPC, ra
        ERET

        .data
data:   .word   0x44332211

        .bss
log:    .space  512
log_user:
        .space  256
