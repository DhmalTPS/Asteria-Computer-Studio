; Conformance: loads, stores and SWAP (spec 4.4, 4.4a). Memory is little-endian.
;   LB LH LW LBU LHU SB SH SW SWAP
; Passes by halting with a0 = 0. On failure a0 = the number of the failed check.

        .text
        .global main
main:
        LA      s0, words
        LA      s1, buf
        LI      s2, -1

        LI      a0, 1                   ; LW reads little-endian
        LW      t0, 0(s0)
        LI      t1, 0x11223344
        BNE     t0, t1, fail

        LI      a0, 2                   ; LB sign-extends, LBU zero-extends
        LB      t0, 0(s0)
        LI      t1, 0x44
        BNE     t0, t1, fail
        LB      t0, 4(s0)               ; 0xBB
        LI      t1, 0xFFFFFFBB
        BNE     t0, t1, fail
        LBU     t0, 4(s0)
        LI      t1, 0xBB
        BNE     t0, t1, fail
        LB      t0, 7(s0)               ; 0x88
        LI      t1, 0xFFFFFF88
        BNE     t0, t1, fail

        LI      a0, 3                   ; LH sign-extends, LHU zero-extends
        LH      t0, 0(s0)
        LI      t1, 0x3344
        BNE     t0, t1, fail
        LH      t0, 2(s0)
        LI      t1, 0x1122
        BNE     t0, t1, fail
        LH      t0, 4(s0)               ; 0xAABB
        LI      t1, 0xFFFFAABB
        BNE     t0, t1, fail
        LHU     t0, 4(s0)
        LI      t1, 0x0000AABB
        BNE     t0, t1, fail
        LH      t0, 6(s0)               ; 0x8899
        LI      t1, 0xFFFF8899
        BNE     t0, t1, fail

        LI      a0, 4                   ; offsets are signed
        ADDI    t2, s0, 8
        LW      t0, -4(t2)
        LI      t1, 0x8899AABB
        BNE     t0, t1, fail

        LI      a0, 5                   ; SW, then SB and SH replace only their bytes
        LI      t0, 0xDEADBEEF
        SW      t0, 0(s1)
        LI      t0, 0x1234              ; SB stores rs2[7:0] = 0x34
        SB      t0, 1(s1)
        LW      t1, 0(s1)
        LI      t2, 0xDEAD34EF
        BNE     t1, t2, fail
        LI      t0, 0xABCD5678          ; SH stores rs2[15:0] = 0x5678
        SH      t0, 2(s1)
        LW      t1, 0(s1)
        LI      t2, 0x567834EF
        BNE     t1, t2, fail
        LBU     t1, 3(s1)
        LI      t2, 0x56
        BNE     t1, t2, fail

        LI      a0, 6                   ; SWAP: rd <- old memory, memory <- rs2
        ADDI    s3, s1, 4               ; buf+4 starts as 0
        LI      t1, 7
        SWAP    t0, t1, (s3)
        BNEZ    t0, fail
        LW      t2, 0(s3)
        BNE     t2, t1, fail
        LI      t0, 9                   ; rd = rs2: the source is read before rd is written
        SWAP    t0, t0, (s3)
        BNE     t0, t1, fail            ; got the old 7
        LW      t2, 0(s3)
        LI      t1, 9
        BNE     t2, t1, fail
        MOV     t3, s3                  ; rd = rs1
        LI      t1, 5
        SWAP    t3, t1, (t3)
        LI      t2, 9
        BNE     t3, t2, fail
        LW      t2, 0(s3)
        BNE     t2, t1, fail

        LI      a0, 7                   ; a load into R0 is discarded
        LW      zero, 0(s0)
        BNEZ    zero, fail

        LI      a0, 8                   ; read-only data is readable
        LA      t2, table
        LW      t0, 8(t2)
        LI      t1, 30
        BNE     t0, t1, fail
        LBU     t0, 12(t2)              ; "A"
        LI      t1, 'A'
        BNE     t0, t1, fail

        LI      a0, 9                   ; .bss starts zeroed
        LA      t2, zeros
        LW      t0, 0(t2)
        BNEZ    t0, fail
        LW      t0, 252(t2)
        BNEZ    t0, fail

        LI      a0, 10                  ; code written at run time runs after FENCE.I (spec 9)
        LA      t2, patch
        LI      t0, 0x002A0144          ; ADDI a1, zero, 42
        SW      t0, 0(t2)
        FENCE.I
patch:  NOP                             ; replaced by the store above
        LI      t1, 42
        BNE     a1, t1, fail

        LI      a0, 0
fail:   HALT

        .rodata
table:  .word   10, 20, 30
        .asciz  "A"

        .data
words:  .word   0x11223344, 0x8899AABB
buf:    .word   0, 0

        .bss
zeros:  .space  256
