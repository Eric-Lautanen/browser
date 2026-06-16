#include "test_framework.hpp"
#include "../js/jit.hpp"
#include "../js/vm.hpp"
#include "../js/bytecode.hpp"
#include "../js/gc.hpp"

using namespace browser::js;
using namespace browser;

TEST(x64_emit_mov, {
    X64Assembler asm_;
    asm_.mov(Reg64::RAX, Reg64::RBX);
    ASSERT_EQ(asm_.code_size(), 3u);
    ASSERT_EQ(asm_.code()[0], 0x48);
    ASSERT_EQ(asm_.code()[1], 0x89);
    ASSERT_EQ(asm_.code()[2], 0xD8);
})

TEST(x64_emit_ret, {
    X64Assembler asm_;
    asm_.ret();
    ASSERT_EQ(asm_.code_size(), 1u);
    ASSERT_EQ(asm_.code()[0], 0xC3);
})

TEST(x64_emit_push_pop, {
    X64Assembler asm_;
    asm_.push(Reg64::RAX);
    asm_.pop(Reg64::RCX);
    ASSERT(asm_.code_size() > 0);
})

TEST(x64_emit_jmp, {
    X64Assembler asm_;
    asm_.jmp(0);
    ASSERT_EQ(asm_.code_size(), 5u);
    ASSERT_EQ(asm_.code()[0], 0xE9);
    ASSERT_EQ(asm_.code()[1], 0x00);
    ASSERT_EQ(asm_.code()[2], 0x00);
    ASSERT_EQ(asm_.code()[3], 0x00);
    ASSERT_EQ(asm_.code()[4], 0x00);
})

TEST(exec_mem_alloc, {
    ExecutableMemory mem;
    u8* p = mem.allocate(4096);
    ASSERT(p != nullptr);
    p[0] = 0xC3;
    mem.protect();
    auto fn = (void(*)())p;
    fn();
})

TEST(jit_prologue_epilogue, {
    JITCompiler jit;
    VM vm;
    jit.set_vm(&vm);
    BytecodeFunction func;
    func.num_locals = 0;
    func.instructions.push_back({Opcode::RETURN, u32{0}});
    auto code = jit.compile(&func);
    ASSERT(code != nullptr);
    auto fn = (void(*)(VM*))code;
    fn(&vm);
})

TEST(jit_push_number, {
    JITCompiler jit;
    VM vm;
    jit.set_vm(&vm);
    BytecodeFunction func;
    func.num_locals = 0;
    BytecodeFunction::Constant c;
    c.type = BytecodeFunction::Constant::Type::NUMBER;
    c.number = 42.0;
    func.constants.push_back(c);
    func.instructions.push_back({Opcode::PUSH_NUMBER, u32{0}});
    func.instructions.push_back({Opcode::RETURN, u32{0}});
    auto code = jit.compile(&func);
    ASSERT(code != nullptr);
    auto fn = (void(*)(VM*))code;
    fn(&vm);
    auto result = vm.pop();
    ASSERT_EQ(result.number_val, 42.0);
})

TEST(jit_jump, {
    JITCompiler jit;
    VM vm;
    jit.set_vm(&vm);
    BytecodeFunction func;
    func.num_locals = 0;
    BytecodeFunction::Constant c1, c2;
    c1.type = BytecodeFunction::Constant::Type::NUMBER;
    c1.number = 1.0;
    c2.type = BytecodeFunction::Constant::Type::NUMBER;
    c2.number = 2.0;
    func.constants.push_back(c1);
    func.constants.push_back(c2);
    func.instructions.push_back({Opcode::PUSH_NUMBER, u32{0}});
    func.instructions.push_back({Opcode::JMP, u32{3}});
    func.instructions.push_back({Opcode::PUSH_NUMBER, u32{1}});
    func.instructions.push_back({Opcode::RETURN, u32{0}});
    auto code = jit.compile(&func);
    ASSERT(code != nullptr);
    auto fn = (void(*)(VM*))code;
    fn(&vm);
    auto result = vm.pop();
    ASSERT_EQ(result.number_val, 1.0);
})

TEST(jit_add, {
    JITCompiler jit;
    VM vm;
    jit.set_vm(&vm);
    BytecodeFunction func;
    func.num_locals = 0;
    BytecodeFunction::Constant c1, c2;
    c1.type = BytecodeFunction::Constant::Type::NUMBER;
    c1.number = 1.0;
    c2.type = BytecodeFunction::Constant::Type::NUMBER;
    c2.number = 2.0;
    func.constants.push_back(c1);
    func.constants.push_back(c2);
    func.instructions.push_back({Opcode::PUSH_NUMBER, u32{0}});
    func.instructions.push_back({Opcode::PUSH_NUMBER, u32{1}});
    func.instructions.push_back({Opcode::ADD, u32{0}});
    func.instructions.push_back({Opcode::RETURN, u32{0}});
    auto code = jit.compile(&func);
    ASSERT(code != nullptr);
    auto fn = (void(*)(VM*))code;
    fn(&vm);
    auto result = vm.pop();
    ASSERT_EQ(result.number_val, 3.0);
})

TEST(jit_tier_up, {
    VM vm;
    JITCompiler jit;
    jit.set_vm(&vm);
    vm.jit_state_.compiler = &jit;
    BytecodeFunction func;
    func.num_locals = 0;
    BytecodeFunction::Constant c;
    c.type = BytecodeFunction::Constant::Type::NUMBER;
    c.number = 42.0;
    func.constants.push_back(c);
    func.instructions.push_back({Opcode::PUSH_NUMBER, u32{0}});
    func.instructions.push_back({Opcode::RETURN, u32{0}});
    for (int i = 0; i < 150; i++) {
        vm.execute(&func);
    }
    ASSERT(vm.jit_state_.jit_entries.count(&func) > 0);
})
