; Conformance: virtual memory (spec 8): the two-level walk, superpages, permissions,
; A/D bits, the page-fault subcodes, User-mode access and the TLB rules.
;
; Layout: RAM 0x80000000-0x803FFFFF is identity-mapped by one kernel superpage, so the kernel,
; its data and the page tables keep their addresses once paging is on. Test pages live at
; VA 0x00400000 + n * 0x1000 (root[1] -> l2[n]):
;   l2[0] frame_a, R W           l2[4] a pointer at level 0 (invalid)
;   l2[1] nothing (V = 0)        l2[5] user_page (the code below), R X U
;   l2[2] frame_b, R only        l2[6] udata, R W U
;   l2[3] frame_b, W only        l2[7] frame_b, R W
;   l2[8] frame_c, R W, A preset l2[9] kpage (kernel code), X only
; root[2] is a superpage whose PPN is not 4 MiB-aligned; root[3] is empty; root[4] points to
; a second-level table in unpopulated memory; root[5] maps VA 0x01400000 onto PA 0x80000000, a
; superpage that is NOT the identity, so VPN0 and the offset must pass through.
;
; Every PTE is written before paging is turned on, so no stale translation can exist (spec 8.6),
; except in check 12, which tests exactly that rule.
;
; The handler logs each trap (CAUSE, EPC, BADVADDR, STATUS) and returns past the trapping
; instruction; a failed fetch returns to ra; ECALL from User mode returns to the kernel at s5.
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .equ    V, 0x01
        .equ    R, 0x02
        .equ    W, 0x04
        .equ    X, 0x08
        .equ    U, 0x10
        .equ    A, 0x40
        .equ    D, 0x80
        .equ    LOAD_PF, 9
        .equ    STORE_PF, 10
        .equ    SUB_PERMISSION, 0x100
        .equ    SUB_SUPERPAGE, 0x200

        .text
        .global main
main:
        LA      t0, handler
        CSRW    TVEC, t0
        LA      s4, log
        LA      s0, root
        LA      s1, l2
        LA      s2, frame_a
        LA      s3, frame_b

        ; root[512]: VA 0x80000000 -> PA 0x80000000, a 4 MiB kernel superpage. A and D are
        ; preset, so the walker never writes this entry.
        LI      t0, 0x80000000 + D + A + X + W + R + V
        SW      t0, 2048(s0)
        ORI     t0, s1, V               ; root[1]: pointer to l2
        SW      t0, 4(s0)
        LI      t0, 0x80001000 + R + V  ; root[2]: PPN 0x80001, not a multiple of 1024
        SW      t0, 8(s0)
        LI      t0, 0x40000000 + V      ; root[4]: a pointer to a table where nothing is
        SW      t0, 16(s0)

        ORI     t0, s2, R + W + V
        SW      t0, 0(s1)               ; l2[0]
        ORI     t0, s3, R + V
        SW      t0, 8(s1)               ; l2[2]
        ORI     t0, s3, W + V
        SW      t0, 12(s1)              ; l2[3]
        ORI     t0, s3, V
        SW      t0, 16(s1)              ; l2[4]: R = W = X = 0 means "pointer"
        LA      t0, user_page
        ORI     t0, t0, R + X + U + V
        SW      t0, 20(s1)              ; l2[5]
        LA      t0, udata
        ORI     t0, t0, R + W + U + V
        SW      t0, 24(s1)              ; l2[6]
        ORI     t0, s3, R + W + V
        SW      t0, 28(s1)              ; l2[7]
        LA      t0, frame_c
        ORI     t0, t0, A + W + R + V
        SW      t0, 32(s1)              ; l2[8]: A already set, D not
        LA      t0, kpage
        ORI     t0, t0, X + V
        SW      t0, 36(s1)              ; l2[9]: execute-only, kernel
        LI      t0, 0x80000000 + W + R + V
        SW      t0, 20(s0)              ; root[5]: superpage, A = D = 0

        ORI     t0, s0, 1               ; PTBR: root PPN, EN = 1
        CSRW    PTBR, t0                ; paging is on from the next fetch
        LI      s5, 0x00400000          ; the first test page

        LI      a0, 1                   ; a store through a 4 KiB page reaches its frame
        LI      t0, 0xCAFE
        SW      t0, 8(s5)
        LW      t1, 8(s2)               ; frame_a, through the identity map
        BNE     t0, t1, fail
        LW      t1, 8(s5)
        BNE     t0, t1, fail

        LI      a0, 2                   ; the store set A and D in l2[0]
        LW      t0, 0(s1)
        ORI     t1, s2, D + A + W + R + V
        BNE     t0, t1, fail

        LI      a0, 3                   ; V = 0: LoadPageFault, subcode 0, rd unchanged
        LI      t2, 0x77
t3_at:  LW      t2, 0x1000(s5)
        LW      t0, -16(s4)
        LI      t1, LOAD_PF
        BNE     t0, t1, fail
        LW      t0, -12(s4)
        LA      t1, t3_at
        BNE     t0, t1, fail
        LW      t0, -8(s4)              ; BADVADDR is the virtual address
        LI      t1, 0x00401000
        BNE     t0, t1, fail
        LI      t1, 0x77
        BNE     t2, t1, fail

        LI      a0, 4                   ; a store to a read-only page: subcode 1
        LI      t1, 0x600D
        SW      t1, 0(s3)               ; frame_b[0], through the identity map
        SW      t0, 0x2000(s5)
        LW      t0, -16(s4)
        LI      t1, STORE_PF + SUB_PERMISSION
        BNE     t0, t1, fail
        ADDI    t3, s5, 0x2000          ; SWAP counts as a store: it needs R and W
t4_swap: SWAP   t2, t2, (t3)
        LW      t0, -16(s4)
        BNE     t0, t1, fail
        LW      t0, -12(s4)             ; it really trapped (the code alone could be the SW's)
        LA      t1, t4_swap
        BNE     t0, t1, fail
        LW      t0, 0(s3)               ; neither fault wrote memory
        LI      t1, 0x600D
        BNE     t0, t1, fail
        LW      t0, 8(s1)               ; faulting accesses set neither A nor D
        ORI     t1, s3, R + V
        BNE     t0, t1, fail

        LI      a0, 5                   ; a load from the read-only page sets A, not D
        LW      t0, 0x2000(s5)
        LW      t0, 8(s1)
        ORI     t1, s3, A + R + V
        BNE     t0, t1, fail

        LI      a0, 6                   ; W without R is reserved: subcode 1
        LW      t0, 0x3000(s5)
        LW      t0, -16(s4)
        LI      t1, LOAD_PF + SUB_PERMISSION
        BNE     t0, t1, fail

        LI      a0, 7                   ; a pointer at level 0: subcode 0
        LW      t0, 0x4000(s5)
        LW      t0, -16(s4)
        LI      t1, LOAD_PF
        BNE     t0, t1, fail

        LI      a0, 8                   ; a misaligned superpage: subcode 2
        LI      t3, 0x00800000
        LW      t0, 0(t3)
        LW      t0, -16(s4)
        LI      t1, LOAD_PF + SUB_SUPERPAGE
        BNE     t0, t1, fail

        LI      a0, 9                   ; an empty root entry: subcode 0
        LI      t3, 0x00C00000
        LW      t0, 0(t3)
        LW      t0, -16(s4)
        LI      t1, LOAD_PF
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        BNE     t0, t3, fail

        LI      a0, 14                  ; alignment is checked before translation (spec 4.4)
        LW      t0, 2(t3)               ; t3 = 0x00C00002 is unmapped AND misaligned
        LW      t0, -16(s4)
        LI      t1, 7                   ; LoadMisaligned, not a page fault
        BNE     t0, t1, fail

        LI      a0, 15                  ; a bus error during the walk: the access's BusError,
        LI      t3, 0x01000000          ; with BADVADDR = the virtual address (spec 8.4)
        LW      t0, 0(t3)               ; root[4] points at unpopulated memory
        LW      t0, -16(s4)
        LI      t1, 11                  ; LoadBusError
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        BNE     t0, t3, fail
        LI      t3, 0x00C00000

        LI      a0, 10                  ; fetches need X: InstrPageFault, EPC = BADVADDR = target
        JALR    ra, t3, 0               ; t3 = 0x00C00000: not mapped
        LW      t0, -16(s4)
        LI      t1, 1
        BNE     t0, t1, fail
        LW      t0, -12(s4)
        BNE     t0, t3, fail
        JALR    ra, s5, 0               ; l2[0] is R W, not X
        LW      t0, -16(s4)
        LI      t1, 1 + SUB_PERMISSION
        BNE     t0, t1, fail
        LW      t0, -8(s4)
        BNE     t0, s5, fail

        LI      a0, 11                  ; SWAP through a R W page works and sets A and D
        LI      t0, 0x1111
        SW      t0, 0(s3)
        LI      t3, 0x00407000
        LI      t2, 0x2222
        SWAP    t1, t2, (t3)
        LI      t0, 0x1111
        BNE     t1, t0, fail
        LW      t0, 0(s3)
        BNE     t0, t2, fail
        LW      t0, 28(s1)
        ORI     t1, s3, D + A + W + R + V
        BNE     t0, t1, fail

        LI      a0, 12                  ; remapping a page needs TLBFLUSH, then takes effect
        LI      t0, 0xBEEF
        SW      t0, 8(s3)               ; frame_b + 8
        ORI     t0, s3, R + W + V
        SW      t0, 0(s1)               ; l2[0] now points at frame_b
        TLBFLUSH zero, zero
        LW      t1, 8(s5)               ; was 0xCAFE in frame_a
        LI      t0, 0xBEEF
        BNE     t1, t0, fail

        LI      a0, 16                  ; A already set: a store sets D, and only D
        LI      t3, 0x00408000
        SW      t3, 0(t3)
        LW      t0, 32(s1)
        LA      t1, frame_c
        ORI     t1, t1, D + A + W + R + V
        BNE     t0, t1, fail

        LI      a0, 17                  ; execute-only: X does not imply R; a fetch sets A
        LI      t3, 0x00409000
t17_load: LW    t0, 0(t3)
        LW      t0, -16(s4)
        LI      t1, LOAD_PF + SUB_PERMISSION
        BNE     t0, t1, fail
        LW      t0, -12(s4)
        LA      t1, t17_load
        BNE     t0, t1, fail
        LW      t0, 36(s1)              ; the faulting load set no A
        LA      t2, kpage
        ORI     t1, t2, X + V
        BNE     t0, t1, fail
        MOV     v0, s4
        JALR    ra, t3, 0               ; kpage is just RET
        BNE     s4, v0, fail            ; the fetch did not trap
        LW      t0, 36(s1)
        ORI     t1, t2, A + X + V
        BNE     t0, t1, fail

        LI      a0, 18                  ; a superpage that is not the identity; A, D at level 1
        LA      t2, frame_c
        LI      t0, 0x5AFE
        SW      t0, 4(t2)               ; through the identity map
        LI      t0, 0x81400000          ; VA = PA - 0x80000000 + 0x01400000
        ADD     t3, t2, t0
        LW      t1, 4(t3)
        LI      t0, 0x5AFE
        BNE     t1, t0, fail
        LW      t0, 20(s0)              ; the load set A in the root entry
        LI      t1, 0x80000000 + A + W + R + V
        BNE     t0, t1, fail
        LI      t0, 0x1357
        SW      t0, 8(t3)               ; and a store sets D
        LW      t1, 8(t2)
        BNE     t1, t0, fail
        LW      t0, 20(s0)
        LI      t1, 0x80000000 + D + A + W + R + V
        BNE     t0, t1, fail

        LI      a0, 19                  ; Kernel mode may access a U page (spec 6)
        LI      t3, 0x00406000
        MOV     v0, s4
        LW      t0, 0(t3)
        SW      t0, 4(t3)
        BNE     s4, v0, fail

        LI      a0, 13                  ; User mode: U pages only
        LA      t0, udata
        LI      t1, 0x5A5A
        SW      t1, 0(t0)
        LI      t0, 0x00405000          ; the VA of user_page
        CSRW    EPC, t0
        CSRRCI  zero, STATUS, 12        ; PIE = PKM = 0: ERET enters User mode
        LA      s6, user_log
        MOV     s4, s6
        LA      s7, back_in_kernel
        LI      t2, 0x77
        ERET

back_in_kernel:
        LI      t0, 0x5A5A              ; the user read its own page
        BNE     t1, t0, fail
        LI      t0, 0x77                ; the faulting user load did not write t2
        BNE     t2, t0, fail
        SUB     t0, s4, s6              ; four traps: load, store and fetch faults, then the ECALL
        LI      t1, 64
        BNE     t0, t1, fail
        LW      t0, 0(s6)
        LI      t1, LOAD_PF + SUB_PERMISSION
        BNE     t0, t1, fail
        LW      t0, 4(s6)               ; EPC is the virtual address of the load
        LA      t1, u_fault
        LA      t3, user_page
        SUB     t1, t1, t3
        LI      t3, 0x00405000
        ADD     t1, t1, t3
        BNE     t0, t1, fail
        LW      t0, 8(s6)
        BNE     t0, s5, fail            ; 0x00400000
        LW      t0, 16(s6)              ; a store to a U page without W
        LI      t1, STORE_PF + SUB_PERMISSION
        BNE     t0, t1, fail
        LW      t0, 24(s6)
        LI      t1, 0x00405000
        BNE     t0, t1, fail
        LW      t0, 32(s6)              ; a fetch from a page with X but U = 0
        LI      t1, 1 + SUB_PERMISSION
        BNE     t0, t1, fail
        LW      t0, 36(s6)              ; EPC = BADVADDR = the target
        LI      t1, 0x00409000
        BNE     t0, t1, fail
        LW      t0, 40(s6)
        BNE     t0, t1, fail
        LW      t0, 48(s6)
        LI      t1, 6
        BNE     t0, t1, fail

        LI      a0, 0
        CSRW    PTBR, zero              ; paging off again
fail:   HALT

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
        JR      s7                      ; from User mode: continue in the kernel
.Lnext: CSRR    t9, EPC
        ADDI    t9, t9, 4
        CSRW    EPC, t9
        ERET
.Lto_ra:
        CSRW    EPC, ra
        ERET

; ---- one page of User-mode code, mapped at VA 0x00405000 ------------------------------------
        .align  12
user_page:
        LI      t0, 0x00406000          ; udata: U = 1
        LW      t1, 0(t0)
        LI      t0, 0x00400000          ; l2[0]: U = 0
u_fault:
        LW      t2, 0(t0)               ; LoadPageFault, permission (U = 0)
        LI      t0, 0x00405000
        SW      t1, 0(t0)               ; StorePageFault, permission (this page has no W)
        LI      t0, 0x00409000
        JALR    ra, t0, 0               ; InstrPageFault, permission (X, but U = 0)
        ECALL

; ---- one page of kernel code, mapped execute-only at VA 0x00409000 --------------------------
        .align  12
kpage:  RET

        .bss
root:   .space  4096
l2:     .space  4096
frame_a:
        .space  4096
frame_b:
        .space  4096
udata:  .space  4096
frame_c:
        .space  4096
log:    .space  512
user_log:
        .space  256
