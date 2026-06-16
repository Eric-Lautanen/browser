#include "jit.hpp"
#include "bytecode.hpp"

#if defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <cstring>
#include <cstddef>
#include <algorithm>

namespace browser::js {

static u8 reg_bits(Reg64 r) { return static_cast<u8>(r) & 7; }
static bool reg_ext(Reg64 r) { return static_cast<u8>(r) >= 8; }
static u8 xmm_bits(XMMReg r) { return static_cast<u8>(r) & 7; }
static bool xmm_ext(XMMReg r) { return static_cast<u8>(r) >= 8; }

X64Assembler::X64Assembler() {}

void X64Assembler::emit8(u8 b) { code_.push_back(b); }

void X64Assembler::emit32(u32 v) {
    code_.push_back((u8)(v));
    code_.push_back((u8)(v >> 8));
    code_.push_back((u8)(v >> 16));
    code_.push_back((u8)(v >> 24));
}

void X64Assembler::emit64(u64 v) {
    for (int i = 0; i < 8; i++)
        code_.push_back((u8)(v >> (i * 8)));
}

void X64Assembler::emit_rex(bool w, bool r, bool x, bool b) {
    u8 rex = 0x40;
    if (w) rex |= 0x08;
    if (r) rex |= 0x04;
    if (x) rex |= 0x02;
    if (b) rex |= 0x01;
    emit8(rex);
}

void X64Assembler::emit_modrm(u8 mod, u8 reg, u8 rm) {
    emit8((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

void X64Assembler::emit_sib(u8 scale, u8 index, u8 base) {
    emit8((scale << 6) | (index << 3) | base);
}

void X64Assembler::mov(Reg64 dst, Reg64 src) {
    emit_rex(true, reg_ext(src), false, reg_ext(dst));
    emit8(0x89);
    emit_modrm(3, reg_bits(src), reg_bits(dst));
}

void X64Assembler::mov(Reg64 dst, u32 imm32) {
    emit_rex(true, false, false, reg_ext(dst));
    emit8(0xC7);
    emit_modrm(3, 0, reg_bits(dst));
    emit32(imm32);
}

void X64Assembler::mov_abs(Reg64 dst, u64 imm) {
    u8 rex = 0x48;
    if (reg_ext(dst)) rex |= 0x01;
    emit8(rex);
    emit8(0xB8 | reg_bits(dst));
    emit64(imm);
}

void X64Assembler::mov_to_mem(Reg64 addr, Reg64 src) {
    emit_rex(true, reg_ext(src), false, reg_ext(addr));
    emit8(0x89);
    emit_modrm(0, reg_bits(src), reg_bits(addr));
}

void X64Assembler::mov_from_mem(Reg64 dst, Reg64 addr) {
    emit_rex(true, reg_ext(dst), false, reg_ext(addr));
    emit8(0x8B);
    emit_modrm(0, reg_bits(dst), reg_bits(addr));
}

void X64Assembler::push(Reg64 reg) {
    if (reg_ext(reg)) emit8(0x41);
    emit8(0x50 | reg_bits(reg));
}

void X64Assembler::pop(Reg64 reg) {
    if (reg_ext(reg)) emit8(0x41);
    emit8(0x58 | reg_bits(reg));
}

void X64Assembler::add(Reg64 dst, Reg64 src) {
    emit_rex(true, reg_ext(src), false, reg_ext(dst));
    emit8(0x01);
    emit_modrm(3, reg_bits(src), reg_bits(dst));
}

void X64Assembler::add(Reg64 dst, u32 imm) {
    emit_rex(true, false, false, reg_ext(dst));
    emit8(0x81);
    emit_modrm(3, 0, reg_bits(dst));
    emit32(imm);
}

void X64Assembler::sub(Reg64 dst, Reg64 src) {
    emit_rex(true, reg_ext(src), false, reg_ext(dst));
    emit8(0x29);
    emit_modrm(3, reg_bits(src), reg_bits(dst));
}

void X64Assembler::sub(Reg64 dst, u32 imm) {
    emit_rex(true, false, false, reg_ext(dst));
    emit8(0x81);
    emit_modrm(3, 5, reg_bits(dst));
    emit32(imm);
}

void X64Assembler::cmp(Reg64 a, Reg64 b) {
    emit_rex(true, reg_ext(b), false, reg_ext(a));
    emit8(0x39);
    emit_modrm(3, reg_bits(b), reg_bits(a));
}

void X64Assembler::cmp(Reg64 a, u32 imm) {
    emit_rex(true, false, false, reg_ext(a));
    emit8(0x81);
    emit_modrm(3, 7, reg_bits(a));
    emit32(imm);
}

void X64Assembler::jmp(u32 offset) {
    emit8(0xE9);
    emit32(offset);
}

void X64Assembler::je(u32 offset) {
    emit8(0x0F);
    emit8(0x84);
    emit32(offset);
}

void X64Assembler::jne(u32 offset) {
    emit8(0x0F);
    emit8(0x85);
    emit32(offset);
}

void X64Assembler::jl(u32 offset) {
    emit8(0x0F);
    emit8(0x8C);
    emit32(offset);
}

void X64Assembler::jg(u32 offset) {
    emit8(0x0F);
    emit8(0x8F);
    emit32(offset);
}

void X64Assembler::jle(u32 offset) {
    emit8(0x0F);
    emit8(0x8E);
    emit32(offset);
}

void X64Assembler::jge(u32 offset) {
    emit8(0x0F);
    emit8(0x8D);
    emit32(offset);
}

void X64Assembler::jmp(Reg64 target) {
    if (reg_ext(target)) emit8(0x41);
    emit8(0xFF);
    emit_modrm(3, 4, reg_bits(target));
}

void X64Assembler::call(Reg64 target) {
    if (reg_ext(target)) emit8(0x41);
    emit8(0xFF);
    emit_modrm(3, 2, reg_bits(target));
}

void X64Assembler::ret() {
    emit8(0xC3);
}

void X64Assembler::movsd(XMMReg dst, XMMReg src) {
    emit8(0xF2);
    if (xmm_ext(dst) || xmm_ext(src))
        emit_rex(false, xmm_ext(dst), false, xmm_ext(src));
    emit8(0x0F);
    emit8(0x10);
    emit_modrm(3, xmm_bits(dst), xmm_bits(src));
}

void X64Assembler::addsd(XMMReg dst, XMMReg src) {
    emit8(0xF2);
    if (xmm_ext(dst) || xmm_ext(src))
        emit_rex(false, xmm_ext(dst), false, xmm_ext(src));
    emit8(0x0F);
    emit8(0x58);
    emit_modrm(3, xmm_bits(dst), xmm_bits(src));
}

void X64Assembler::subsd(XMMReg dst, XMMReg src) {
    emit8(0xF2);
    if (xmm_ext(dst) || xmm_ext(src))
        emit_rex(false, xmm_ext(dst), false, xmm_ext(src));
    emit8(0x0F);
    emit8(0x5C);
    emit_modrm(3, xmm_bits(dst), xmm_bits(src));
}

void X64Assembler::mulsd(XMMReg dst, XMMReg src) {
    emit8(0xF2);
    if (xmm_ext(dst) || xmm_ext(src))
        emit_rex(false, xmm_ext(dst), false, xmm_ext(src));
    emit8(0x0F);
    emit8(0x59);
    emit_modrm(3, xmm_bits(dst), xmm_bits(src));
}

void X64Assembler::divsd(XMMReg dst, XMMReg src) {
    emit8(0xF2);
    if (xmm_ext(dst) || xmm_ext(src))
        emit_rex(false, xmm_ext(dst), false, xmm_ext(src));
    emit8(0x0F);
    emit8(0x5E);
    emit_modrm(3, xmm_bits(dst), xmm_bits(src));
}

void X64Assembler::cvtsi2sd(XMMReg dst, Reg64 src) {
    emit8(0xF2);
    emit_rex(true, xmm_ext(dst), false, reg_ext(src));
    emit8(0x0F);
    emit8(0x2A);
    emit_modrm(3, xmm_bits(dst), reg_bits(src));
}

void X64Assembler::cvttsd2si(Reg64 dst, XMMReg src) {
    emit8(0xF2);
    emit_rex(true, reg_ext(dst), false, xmm_ext(src));
    emit8(0x0F);
    emit8(0x2C);
    emit_modrm(3, reg_bits(dst), xmm_bits(src));
}

void X64Assembler::ucomisd(XMMReg a, XMMReg b) {
    emit8(0x66);
    if (xmm_ext(a) || xmm_ext(b))
        emit_rex(false, xmm_ext(a), false, xmm_ext(b));
    emit8(0x0F);
    emit8(0x2E);
    emit_modrm(3, xmm_bits(a), xmm_bits(b));
}

void X64Assembler::xorpd(XMMReg dst, XMMReg src) {
    emit8(0x66);
    if (xmm_ext(dst) || xmm_ext(src))
        emit_rex(false, xmm_ext(dst), false, xmm_ext(src));
    emit8(0x0F);
    emit8(0x57);
    emit_modrm(3, xmm_bits(dst), xmm_bits(src));
}

u32 X64Assembler::current_offset() const {
    return (u32)code_.size();
}

const u8* X64Assembler::code() const {
    return code_.data();
}

u32 X64Assembler::code_size() const {
    return (u32)code_.size();
}

void X64Assembler::patch_jump(u32 offset, u32 target) {
    u32 rel = target - (offset + 4);
    code_[offset + 0] = (u8)(rel);
    code_[offset + 1] = (u8)(rel >> 8);
    code_[offset + 2] = (u8)(rel >> 16);
    code_[offset + 3] = (u8)(rel >> 24);
}

void X64Assembler::clear() {
    code_.clear();
    label_fixups_.clear();
    label_offsets_.clear();
    label_bound_.clear();
    next_label_id_ = 0;
}

static void patch_rel32(std::vector<u8>& code, u32 code_offset, u32 target_offset) {
    u32 rel = target_offset - (code_offset + 4);
    code[code_offset + 0] = (u8)(rel);
    code[code_offset + 1] = (u8)(rel >> 8);
    code[code_offset + 2] = (u8)(rel >> 16);
    code[code_offset + 3] = (u8)(rel >> 24);
}

u32 X64Assembler::create_label() {
    u32 id = next_label_id_++;
    if (id >= label_offsets_.size()) {
        label_offsets_.resize(id + 1, UINT32_MAX);
        label_bound_.resize(id + 1, false);
    }
    return id;
}

void X64Assembler::bind_label(u32 id) {
    label_offsets_[id] = current_offset();
    label_bound_[id] = true;
    for (auto it = label_fixups_.begin(); it != label_fixups_.end(); ) {
        if (it->label_id == id) {
            patch_rel32(code_, it->code_offset, label_offsets_[id]);
            it = label_fixups_.erase(it);
        } else {
            ++it;
        }
    }
}

ExecutableMemory::ExecutableMemory() {}

ExecutableMemory::~ExecutableMemory() {
    free_();
}

ExecutableMemory::ExecutableMemory(ExecutableMemory&& other) noexcept
    : base_(other.base_), size_(other.size_) {
    other.base_ = nullptr;
    other.size_ = 0;
}

ExecutableMemory& ExecutableMemory::operator=(ExecutableMemory&& other) noexcept {
    if (this != &other) {
        free_();
        base_ = other.base_;
        size_ = other.size_;
        other.base_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

u8* ExecutableMemory::allocate(u32 size) {
#if defined(_WIN64)
    base_ = (u8*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!base_) return nullptr;
#else
    base_ = (u8*)mmap(nullptr, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base_ == MAP_FAILED) { base_ = nullptr; return nullptr; }
#endif
    size_ = size;
    return base_;
}

void ExecutableMemory::protect() {
#if defined(_WIN64)
    DWORD old;
    VirtualProtect(base_, size_, PAGE_EXECUTE_READ, &old);
#else
    mprotect(base_, size_, PROT_READ | PROT_EXEC);
#endif
}

u8* ExecutableMemory::base() const { return base_; }
u32 ExecutableMemory::size() const { return size_; }

void ExecutableMemory::free_() {
    if (base_) {
#if defined(_WIN64)
        VirtualFree(base_, 0, MEM_RELEASE);
#else
        munmap(base_, size_);
#endif
        base_ = nullptr;
        size_ = 0;
    }
}

JITCompiler::JITCompiler() {}

void JITCompiler::set_vm(VM* vm) {
    vm_ = vm;
}

void JITCompiler::emit_prologue(u32 num_locals) {
    asm_.push(Reg64::RBP);
    asm_.mov(Reg64::RBP, Reg64::RSP);

    frame_size_ = (num_locals * 8 + 15) & ~15;
    if (frame_size_ > 0)
        asm_.sub(Reg64::RSP, frame_size_);

    asm_.push(Reg64::RBX);
    asm_.push(Reg64::RSI);
    asm_.push(Reg64::RDI);
    asm_.push(Reg64::R12);
    asm_.push(Reg64::R13);
    asm_.push(Reg64::R14);
    asm_.push(Reg64::R15);

    asm_.sub(Reg64::RSP, 8);

    asm_.mov(Reg64::RBX, ARG1);
}

void JITCompiler::emit_epilogue() {
    asm_.add(Reg64::RSP, 8);

    asm_.pop(Reg64::R15);
    asm_.pop(Reg64::R14);
    asm_.pop(Reg64::R13);
    asm_.pop(Reg64::R12);
    asm_.pop(Reg64::RDI);
    asm_.pop(Reg64::RSI);
    asm_.pop(Reg64::RBX);

    if (frame_size_ > 0)
        asm_.add(Reg64::RSP, frame_size_);

    asm_.pop(Reg64::RBP);
    asm_.ret();
}

void JITCompiler::patch_fixups(u8* mem) {
    for (auto& fx : fixups_) {
        u32 target_machine = bytecode_to_machine_[fx.target_bc_ip];
        i32 rel = (i32)(target_machine - (fx.code_offset + 4));
        mem[fx.code_offset + 0] = (u8)(rel);
        mem[fx.code_offset + 1] = (u8)(rel >> 8);
        mem[fx.code_offset + 2] = (u8)(rel >> 16);
        mem[fx.code_offset + 3] = (u8)(rel >> 24);
    }
}

bool JITCompiler::try_compile_single(const Instruction& instr) {
    switch (instr.op) {
        case Opcode::PUSH_NUMBER: {
            u32 idx = std::get<u32>(instr.operand);
            f64 val = current_fn_->constants[idx].number;
            u64 bits;
            std::memcpy(&bits, &val, sizeof(bits));
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::RDX, bits);
            asm_.mov_abs(Reg64::R10, (u64)&jit_push_number);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            return true;
        }
        case Opcode::PUSH_UNDEFINED: {
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_push_undefined);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            return true;
        }
        case Opcode::POP: {
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_pop);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            return true;
        }
        case Opcode::ADD: {
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_call_add);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            return true;
        }
        case Opcode::SUB: {
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_call_sub);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            return true;
        }
        case Opcode::MUL: {
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_call_mul);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            return true;
        }
        case Opcode::DIV: {
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_call_div);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            return true;
        }
        case Opcode::MOD: {
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_call_mod);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            return true;
        }
        case Opcode::JMP: {
            u32 target = std::get<u32>(instr.operand);
            asm_.jmp(0);
            fixups_.push_back({asm_.current_offset() - 4, target});
            return true;
        }
        case Opcode::JMP_IF_FALSE: {
            u32 target = std::get<u32>(instr.operand);
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_pop_and_is_truthy);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            asm_.cmp(Reg64::RAX, 0);
            asm_.je(0);
            fixups_.push_back({asm_.current_offset() - 4, target});
            return true;
        }
        case Opcode::JMP_IF_TRUE: {
            u32 target = std::get<u32>(instr.operand);
            asm_.sub(Reg64::RSP, SHADOW_SPACE);
            asm_.mov(Reg64::RCX, Reg64::RBX);
            asm_.mov_abs(Reg64::R10, (u64)&jit_pop_and_is_truthy);
            asm_.call(Reg64::R10);
            asm_.add(Reg64::RSP, SHADOW_SPACE);
            asm_.cmp(Reg64::RAX, 0);
            asm_.jne(0);
            fixups_.push_back({asm_.current_offset() - 4, target});
            return true;
        }
        case Opcode::RETURN: {
            emit_epilogue();
            return true;
        }
        default:
            return false;
    }
}

void* JITCompiler::compile(BytecodeFunction* fn) {
    asm_.clear();
    fixups_.clear();
    bytecode_to_machine_.clear();
    current_fn_ = fn;

    emit_prologue(fn->num_locals);

    bytecode_to_machine_.resize(fn->instructions.size(), 0);
    for (u32 i = 0; i < (u32)fn->instructions.size(); i++) {
        bytecode_to_machine_[i] = asm_.current_offset();
        bool ok = try_compile_single(fn->instructions[i]);
        if (!ok) {
            asm_.clear();
            return nullptr;
        }
    }

    u32 code_size = asm_.code_size();
    if (code_size == 0) return nullptr;

    auto exec_mem = std::make_unique<ExecutableMemory>();
    u8* mem = exec_mem->allocate(code_size + 4096);
    if (!mem) return nullptr;

    std::memcpy(mem, asm_.code(), code_size);

    patch_fixups(mem);

    exec_mem->protect();

    void* entry = exec_mem->base();

    auto it = code_map_.find(fn);
    if (it != code_map_.end())
        it->second = std::move(exec_mem);
    else
        code_map_.emplace(fn, std::move(exec_mem));

    return entry;
}

} // namespace browser::js
