; Conformance: exceptions, trap entry, ERET and privilege, with paging off (spec 4.7, 5, 6).
;
; The handler logs every trap as four words (CAUSE, EPC, BADVADDR, STATUS at entry), then
; returns past the trapping instruction. Two cases return elsewhere:
;   - a failed fetch (InstrPageFault, InstrBusError) returns to ra, the caller of the bad jump;
;   - ECALL from User mode returns to the kernel at s5, staying in Kernel mode.
; Registers: s4 = log pointer, s5 = kernel continuation, gp = the end of RAM (sp at entry);
; t8 and t9 belong to the handler.
;
; Every check that expects a trap also compares the logged EPC: a code alone could be left over from
; the previous trap, so an instruction that wrongly did NOT trap would go unnoticed.
;
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .equ    ILLEGAL, 3
        .equ    PRIVILEGE, 4

        .text
        .global main
main:
        MOV     gp, sp                  ; the loader sets sp to the end of RAM
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
        LA      t0, t7_target           ; misaligned by 1, and by 3 through a negative offset
        LI      t3, 0x99
        LI      t6, 0
        LA      t7, t7_by1
t7_by1: JALR    t3, t0, 1
        CALL    expect_last
        LW      t1, -8(s4)
        LA      t2, t7_target + 1
        BNE     t1, t2, fail
        LA      t0, t7_target
        LA      t7, t7_by3
t7_by3: JALR    t3, t0, -1
        CALL    expect_last
        LW      t1, -8(s4)
        LA      t2, t7_target - 1
        BNE     t1, t2, fail
        LI      t1, 0x99                ; rd was never written
        BNE     t3, t1, fail
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
t8_swap: SWAP   t2, t2, (s2)            ; SWAP fails as a store
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        LW      t0, -12(s4)             ; and it really trapped (the code alone could be the SW's)
        LA      t1, t8_swap
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

        LI      a0, 12                  ; MMIO before M8: every access is a bus error
        LI      s1, 0xF0000000
        LI      t6, 11
        LA      t7, t12_load
t12_load: LW    t2, 0(s1)
        CALL    expect_last
        LW      t0, -8(s4)
        BNE     t0, s1, fail
        LI      t6, 12
        LA      t7, t12_store
t12_store: SW   t2, 0(s1)
        CALL    expect_last
        LA      t7, t12_swap
t12_swap: SWAP  t2, t2, (s1)            ; SWAP on MMIO: StoreBusError
        CALL    expect_last
        LI      t6, 2
        MOV     t7, s1                  ; MMIO is never executable: EPC = the target
        JALR    ra, s1, 0
        CALL    expect_last

        LI      a0, 13                  ; ROM can be read, never written, not even by SWAP
        MOV     v0, s4
        LW      t2, 0x100(zero)
        BNE     s4, v0, fail            ; no trap
        LI      t3, 0x100
        LI      t6, 12
        LA      t7, t13_swap
t13_swap: SWAP  t2, t2, (t3)
        CALL    expect_last
        LW      t0, -8(s4)
        BNE     t0, t3, fail

        LI      a0, 14                  ; the end of RAM, and loads into R0 (spec 4.4)
        MOV     v0, s4
        LW      t2, -4(gp)              ; the last word of RAM
        BNE     s4, v0, fail
        LI      t6, 11
        LA      t7, t14_past
t14_past: LW    t2, 0(gp)               ; one past the end
        CALL    expect_last
        LW      t0, -8(s4)
        BNE     t0, gp, fail
        LA      t7, t14_r0
t14_r0: LW      zero, 0(s1)             ; into R0, yet the access (and its fault) still happens
        CALL    expect_last
        LI      t6, 7
        LA      t7, t14_r0_mis
t14_r0_mis: LW  zero, 2(s0)
        CALL    expect_last

        LI      a0, 15                  ; read-only CSRs: a WRITE is decided by the encoding (4.8)
        LI      t2, 0                   ; writing a zero is still a write
        LI      t3, 0x55
        LI      t6, ILLEGAL
        LA      t7, t15_a
t15_a:  CSRRS   t3, CPUID, t2
        CALL    expect_last
        LA      t7, t15_b
t15_b:  CSRRWI  t3, CPUID, 0
        CALL    expect_last
        LA      t7, t15_c
t15_c:  CSRRW   zero, CYCLE, t2
        CALL    expect_last
        LA      t7, t15_d
t15_d:  CSRRC   t3, INSTRET, t2
        CALL    expect_last
        LA      t7, t15_e
t15_e:  CSRRW   zero, IPEND, t2
        CALL    expect_last
        LI      t0, 0x55                ; rd was never written
        BNE     t3, t0, fail
        MOV     v0, s4
        CSRRS   t3, CPUID, zero         ; rs1 field = 0: a read, legal
        CSRRCI  t2, CPUID, 0            ; uimm5 = 0: a read, legal
        BNE     s4, v0, fail
        LI      t0, 0x00010000
        BNE     t3, t0, fail
        BNE     t2, t0, fail

        LI      a0, 16                  ; ERET honours PIE and PKM written by software
        CSRRSI  zero, STATUS, 12        ; PIE = PKM = 1: STATUS = 0xE
        LA      t0, t16_back
        CSRW    EPC, t0
        ERET
        J       fail
t16_back:
        CSRR    t0, STATUS              ; IE <- PIE = 1, KM <- PKM = 1; PIE, PKM unchanged
        LI      t1, 0xF
        BNE     t0, t1, fail
        CSRRCI  zero, STATUS, 13        ; IE = PIE = PKM = 0
        CSRR    t0, STATUS
        LI      t1, 0x2
        BNE     t0, t1, fail

        LI      a0, 17                  ; User mode, part 2: what User mode may and may not do
        MOV     s6, s4
        LA      t0, user_code2
        CSRW    EPC, t0
        CSRRCI  zero, STATUS, 12
        LA      s5, back_again
        ERET

back_again:
        SUB     t0, s4, s6              ; six traps were logged
        LI      t2, 96
        BNE     t0, t2, fail
        MOV     t5, s6
        LI      t6, ILLEGAL             ; CSRW CYCLE: read-only, so illegal (illegal wins)
        LA      t7, u2_cycle
        CALL    expect_at
        LI      t6, PRIVILEGE           ; CSRW SCRATCH: K-only
        LA      t7, u2_scratch
        CALL    expect_at
        LI      t6, ILLEGAL             ; CSRW IPEND: K-only AND read-only; illegal wins
        LA      t7, u2_ipend
        CALL    expect_at
        LI      t6, PRIVILEGE           ; TLBFLUSH
        LA      t7, u2_tlb
        CALL    expect_at
        LI      t6, 5                   ; EBREAK is allowed: it raises Breakpoint, not Privilege
        LA      t7, u2_ebreak
        CALL    expect_at
        LI      t6, 6                   ; ECALL (FENCE.I before it did not trap)
        LA      t7, u2_ecall
        CALL    expect_at
        BNEZ    s7, fail                ; CYCLEH and INSTRETH read 0 this early
        BNEZ    fp, fail
        BEQZ    v1, fail                ; CYCLE has counted something

        LI      a0, 0
fail:   HALT

; ---- helpers: t6 = expected code, t7 = expected EPC; they use t0 and t5 ----------------------
expect_last:                            ; the last trap logged
        ADDI    t5, s4, -16
expect_at:                              ; the log entry at t5; advances t5 to the next one
        LW      t0, 0(t5)
        BNE     t0, t6, fail
        LW      t0, 4(t5)
        BNE     t0, t7, fail
        ADDI    t5, t5, 16
        RET

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

user_code2:
        CSRR    s7, CYCLEH              ; allowed
        CSRR    fp, INSTRETH            ; allowed
        CSRR    v1, CYCLE               ; allowed
u2_cycle: CSRW  CYCLE, zero             ; IllegalInstruction
u2_scratch: CSRW SCRATCH, zero          ; PrivilegeViolation
u2_ipend: CSRW  IPEND, zero             ; IllegalInstruction
u2_tlb: TLBFLUSH zero, zero             ; PrivilegeViolation
u2_ebreak: EBREAK                       ; Breakpoint
        FENCE.I                         ; allowed
u2_ecall: ECALL

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
        .space  1024
