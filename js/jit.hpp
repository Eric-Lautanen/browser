#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <cstring>
#include <unordered_map>
#include "../tests/utility.hpp"

namespace browser::js {

struct BytecodeFunction;
struct Instruction;
class VM;

enum class Reg64 : u8 {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

enum class XMMReg : u8 {
    XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
    XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15
};

#if defined(_WIN64)
constexpr Reg64 ARG1 = Reg64::RCX;
constexpr Reg64 ARG2 = Reg64::RDX;
constexpr Reg64 ARG3 = Reg64::R8;
constexpr Reg64 ARG4 = Reg64::R9;
constexpr u32 SHADOW_SPACE = 32;
#else
constexpr Reg64 ARG1 = Reg64::RDI;
constexpr Reg64 ARG2 = Reg64::RSI;
constexpr Reg64 ARG3 = Reg64::RDX;
constexpr Reg64 ARG4 = Reg64::RCX;
constexpr u32 SHADOW_SPACE = 0;
#endif

class X64Assembler {
public:
    X64Assembler();

    void emit8(u8 b);
    void emit32(u32 v);
    void emit64(u64 v);

    void mov(Reg64 dst, Reg64 src);
    void mov(Reg64 dst, u32 imm32);
    void mov_abs(Reg64 dst, u64 imm);
    void mov_to_mem(Reg64 addr, Reg64 src);
    void mov_from_mem(Reg64 dst, Reg64 addr);

    void push(Reg64 reg);
    void pop(Reg64 reg);

    void add(Reg64 dst, Reg64 src);
    void add(Reg64 dst, u32 imm);
    void sub(Reg64 dst, Reg64 src);
    void sub(Reg64 dst, u32 imm);
    void cmp(Reg64 a, Reg64 b);
    void cmp(Reg64 a, u32 imm);

    void jmp(u32 offset);
    void je(u32 offset);
    void jne(u32 offset);
    void jl(u32 offset);
    void jg(u32 offset);
    void jle(u32 offset);
    void jge(u32 offset);
    void jmp(Reg64 target);

    void call(Reg64 target);
    void ret();

    void movsd(XMMReg dst, XMMReg src);
    void addsd(XMMReg dst, XMMReg src);
    void subsd(XMMReg dst, XMMReg src);
    void mulsd(XMMReg dst, XMMReg src);
    void divsd(XMMReg dst, XMMReg src);
    void cvtsi2sd(XMMReg dst, Reg64 src);
    void cvttsd2si(Reg64 dst, XMMReg src);
    void ucomisd(XMMReg a, XMMReg b);
    void xorpd(XMMReg dst, XMMReg src);

    u32 current_offset() const;
    const u8* code() const;
    u32 code_size() const;
    void patch_jump(u32 offset, u32 target);
    void clear();

    u32 create_label();
    void bind_label(u32 id);

private:
    std::vector<u8> code_;
    void emit_rex(bool w, bool r, bool x, bool b);
    void emit_modrm(u8 mod, u8 reg, u8 rm);
    void emit_sib(u8 scale, u8 index, u8 base);

    struct LabelFixup {
        u32 code_offset;
        u32 label_id;
    };
    std::vector<LabelFixup> label_fixups_;
    std::vector<u32> label_offsets_;
    std::vector<bool> label_bound_;
    u32 next_label_id_ = 0;
};

class ExecutableMemory {
public:
    ExecutableMemory();
    ~ExecutableMemory();
    ExecutableMemory(const ExecutableMemory&) = delete;
    ExecutableMemory& operator=(const ExecutableMemory&) = delete;
    ExecutableMemory(ExecutableMemory&&) noexcept;
    ExecutableMemory& operator=(ExecutableMemory&&) noexcept;

    u8* allocate(u32 size);
    void protect();
    u8* base() const;
    u32 size() const;

private:
    u8* base_ = nullptr;
    u32 size_ = 0;
    void free_();
};

struct JumpFixup {
    u32 code_offset;
    u32 target_bc_ip;
};

class JITCompiler {
public:
    JITCompiler();
    void* compile(BytecodeFunction* fn);
    void set_vm(VM* vm);

private:
    VM* vm_ = nullptr;
    BytecodeFunction* current_fn_ = nullptr;
    X64Assembler asm_;
    std::vector<JumpFixup> fixups_;
    std::vector<u32> bytecode_to_machine_;
    std::unordered_map<BytecodeFunction*, std::unique_ptr<ExecutableMemory>> code_map_;

    u32 frame_size_ = 0;
    void emit_prologue(u32 num_locals);
    void emit_epilogue();
    bool try_compile_single(const Instruction& instr);
    void patch_fixups(u8* mem);
};

struct JITState {
    std::unordered_map<BytecodeFunction*, u32> call_counts;
    std::unordered_map<BytecodeFunction*, void*> jit_entries;
    JITCompiler* compiler = nullptr;
};

extern "C" {
void jit_push_number(browser::js::VM* vm, u64 val_bits);
void jit_push_undefined(browser::js::VM* vm);
void jit_pop(browser::js::VM* vm);
void jit_call_add(browser::js::VM* vm);
void jit_call_sub(browser::js::VM* vm);
void jit_call_mul(browser::js::VM* vm);
void jit_call_div(browser::js::VM* vm);
void jit_call_mod(browser::js::VM* vm);
u32 jit_pop_and_is_truthy(browser::js::VM* vm);
}

} // namespace browser::js
