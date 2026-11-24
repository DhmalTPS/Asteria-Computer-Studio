#include "cpu/core/semantics.hpp"

#include "common/assert.hpp"
#include "common/bits.hpp"
#include "cpu/core/alu.hpp"
#include "isa/instruction_set.hpp"

namespace asteria::cpu::core {

using isa::Exception;
using isa::Op;

namespace {

struct LoadShape {
    unsigned size;
    bool sign_extend;
};

LoadShape load_shape(Op op) {
    switch (op) {
        case Op::Lb: return {1, true};
        case Op::Lbu: return {1, false};
        case Op::Lh: return {2, true};
        case Op::Lhu: return {2, false};
        case Op::Lw: return {4, false};
        default: ASTERIA_ASSERT(false, "not a load"); return {4, false};
    }
}

unsigned store_size(Op op) {
    switch (op) {
        case Op::Sb: return 1;
        case Op::Sh: return 2;
        case Op::Sw: return 4;
        default: ASTERIA_ASSERT(false, "not a store"); return 4;
    }
}

// Branch and JAL offsets count instructions (spec §4.5, §4.6). u32 arithmetic wraps, which
// is exactly the modulo-2^32 address arithmetic the spec requires.
u32 relative_target(u32 pc, i32 offset_in_instructions) {
    return pc + static_cast<u32>(offset_in_instructions) * 4u;
}

Outcome execute_csr(const isa::Instruction& in, u32 pc, ExecContext& ctx) {
    RegisterFile& r = ctx.regs();
    CsrFile& csr = ctx.csrs();
    const auto number = static_cast<u16>(in.imm);

    const bool immediate_form = in.op == Op::Csrrwi || in.op == Op::Csrrsi || in.op == Op::Csrrci;
    const bool is_write = in.op == Op::Csrrw || in.op == Op::Csrrwi;
    const bool is_set = in.op == Op::Csrrs || in.op == Op::Csrrsi;

    // For the immediate forms the rs1 FIELD is the 5-bit operand itself (§4.8).
    const u32 operand = immediate_form ? u32{in.rs1} : r.read(in.rs1);

    // §4.8: CSRRW with rd = R0 does not read; CSRRS/CSRRC with rs1 = R0 (or uimm5 = 0) do
    // not write. "Does not write" matters: reading a read-only counter must stay legal.
    const bool reads = !(is_write && in.rd == isa::reg::kZero);
    const bool writes = is_write || in.rs1 != 0;

    if (const std::optional<Exception> refused = csr.check_access(number, writes)) {
        return Outcome::raise(Trap::exception(*refused));
    }

    const u32 old = reads ? csr.read(number) : 0;
    if (writes) {
        const u32 value = is_write ? operand : is_set ? (old | operand) : (old & ~operand);
        csr.write(number, value);
    }
    if (reads) {
        r.write(in.rd, old);
    }
    return Outcome::next(pc + 4);
}

}  // namespace

Outcome execute(const isa::Instruction& in, u32 pc, ExecContext& ctx) {
    RegisterFile& r = ctx.regs();
    CsrFile& csr = ctx.csrs();
    const u32 next = pc + 4;

    // §6: privileged instructions. IllegalInstruction (an undecodable word) has a lower
    // code and was already handled by the decoder, so this is the next check in priority.
    if (isa::info(in.op).kernel_only && !csr.kernel_mode()) {
        return Outcome::raise(Trap::exception(Exception::PrivilegeViolation));
    }

    // Source operands are read before anything is written, so rd may equal rs1 or rs2
    // (JALR §4.6, SWAP §4.4a).
    const u32 a = r.read(in.rs1);
    const u32 b = r.read(in.rs2);
    const auto imm = static_cast<u32>(in.imm);  // sign-extended or zero-extended by decode

    switch (in.op) {
        // ---- §4.1, §4.2: register-register ALU ---------------------------------------
        case Op::Add: case Op::Sub: case Op::And: case Op::Or: case Op::Xor: case Op::Nor:
        case Op::Sll: case Op::Srl: case Op::Sra: case Op::Slt: case Op::Sltu:
        case Op::Mul: case Op::Mulh: case Op::Mulhu: case Op::Div: case Op::Divu:
        case Op::Rem: case Op::Remu:
            r.write(in.rd, alu(*alu_op_of(in.op), a, b));
            return Outcome::next(next);

        // ---- §4.3: immediate ALU -----------------------------------------------------
        case Op::Addi: case Op::Slti: case Op::Sltiu: case Op::Andi: case Op::Ori:
        case Op::Xori: case Op::Slli: case Op::Srli: case Op::Srai:
            r.write(in.rd, alu(*alu_op_of(in.op), a, imm));
            return Outcome::next(next);
        case Op::Lui:
            r.write(in.rd, imm << 16);
            return Outcome::next(next);
        case Op::Auipc:
            r.write(in.rd, pc + (imm << 16));
            return Outcome::next(next);

        // ---- §4.4: loads and stores --------------------------------------------------
        case Op::Lb: case Op::Lbu: case Op::Lh: case Op::Lhu: case Op::Lw: {
            const LoadShape shape = load_shape(in.op);
            const u32 ea = a + imm;
            if (!bits::is_aligned(ea, shape.size)) {
                return Outcome::raise(Trap::at(Exception::LoadMisaligned, ea));
            }
            const LoadResult loaded = ctx.load(ea, shape.size);
            if (loaded.trap) {
                return Outcome::raise(*loaded.trap);
            }
            // A load into R0 still performed the access above; only the result is dropped.
            r.write(in.rd, shape.sign_extend ? bits::sign_extend(loaded.value, 8 * shape.size)
                                             : loaded.value);
            return Outcome::next(next);
        }
        case Op::Sb: case Op::Sh: case Op::Sw: {
            const unsigned size = store_size(in.op);
            const u32 ea = a + imm;
            if (!bits::is_aligned(ea, size)) {
                return Outcome::raise(Trap::at(Exception::StoreMisaligned, ea));
            }
            if (const std::optional<Trap> trap = ctx.store(ea, size, b)) {
                return Outcome::raise(*trap);
            }
            return Outcome::next(next);
        }

        // ---- §4.4a: SWAP -------------------------------------------------------------
        case Op::Swap: {
            if (!bits::is_aligned(a, 4)) {
                return Outcome::raise(Trap::at(Exception::StoreMisaligned, a));
            }
            const LoadResult old = ctx.swap(a, b);
            if (old.trap) {
                return Outcome::raise(*old.trap);
            }
            r.write(in.rd, old.value);
            return Outcome::next(next);
        }

        // ---- §4.5, §4.6: control flow ------------------------------------------------
        case Op::Beq: case Op::Bne: case Op::Blt: case Op::Bge: case Op::Bltu: case Op::Bgeu:
            return Outcome::next(*branch_taken(in.op, a, b) ? relative_target(pc, in.imm) : next);
        case Op::Jal:
            r.write(in.rd, next);
            return Outcome::next(relative_target(pc, in.imm));
        case Op::Jalr: {
            const u32 target = a + imm;
            if (!bits::is_aligned(target, 4)) {
                return Outcome::raise(Trap::at(Exception::InstrMisaligned, target));  // rd unchanged
            }
            r.write(in.rd, next);
            return Outcome::next(target);
        }

        // ---- §4.7: system --------------------------------------------------------------
        case Op::Ecall:
            return Outcome::raise(Trap::exception(Exception::EnvironmentCall));
        case Op::Ebreak:
            return Outcome::raise(Trap::exception(Exception::Breakpoint));
        case Op::Eret:
            return Outcome::next(csr.eret());
        case Op::Wfi:
            // Wakes on a pending, enabled line regardless of STATUS.IE (§4.7).
            return csr.pending_enabled() != 0 ? Outcome::next(next) : Outcome::wait();
        case Op::Halt:
            return Outcome::halt();
        case Op::Tlbflush:
            // §8.6: a field naming R0 means "all"; otherwise the register's value selects.
            ctx.tlb_flush(in.rs1 == isa::reg::kZero ? std::nullopt : std::optional<u32>{a},
                          in.rs2 == isa::reg::kZero ? std::nullopt
                                                    : std::optional<u8>{static_cast<u8>(b)});
            return Outcome::next(next);
        case Op::FenceI:
            ctx.fence_i();
            return Outcome::next(next);

        // ---- §4.8: CSR access ----------------------------------------------------------
        case Op::Csrrw: case Op::Csrrs: case Op::Csrrc:
        case Op::Csrrwi: case Op::Csrrsi: case Op::Csrrci:
            return execute_csr(in, pc, ctx);
    }
    ASTERIA_ASSERT(false, "instruction without semantics");
    return Outcome::raise(Trap::exception(Exception::IllegalInstruction));
}

}  // namespace asteria::cpu::core
