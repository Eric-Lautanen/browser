#include "test_framework.hpp"
#include "utility.hpp"
#include "../js/ast.hpp"
#include "../js/parser.hpp"
#include "../js/compiler.hpp"
#include "../js/bytecode.hpp"

using namespace browser;
using namespace browser::js;

static bool has_jump_opcode(BytecodeFunction& bc, Opcode op) {
    for (auto& inst : bc.instructions) {
        if (inst.op == op) return true;
    }
    return false;
}

static u32 instruction_index(BytecodeFunction& bc, Opcode op) {
    for (u32 i = 0; i < bc.instructions.size(); i++) {
        if (bc.instructions[i].op == op) return i;
    }
    return UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

TEST(compile_literal_number, {
    Parser p("42;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_NUMBER);
    u32 idx = std::get<u32>(bc->instructions[0].operand);
    ASSERT_EQ(bc->constants[idx].type, BytecodeFunction::Constant::Type::NUMBER);
    ASSERT_EQ(bc->constants[idx].number, 42);
})

TEST(compile_literal_string, {
    Parser p("\"hello\";");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_STRING);
})

TEST(compile_literal_null, {
    Parser p("null;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_NULL);
})

TEST(compile_literal_undefined, {
    Parser p("undefined;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_UNDEFINED);
})

TEST(compile_literal_bool, {
    Parser p("true;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_BOOL);
})

TEST(compile_literal_bigint, {
    Parser p("123n;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_STRING);
})

TEST(compile_literal_regexp, {
    Parser p("/abc/;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_STRING);
})

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

TEST(compile_ident, {
    Parser p("x;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::LOAD_GLOBAL);
    ASSERT_EQ(std::get<std::string>(bc->instructions[0].operand), "x");
})

TEST(compile_ident_this, {
    Parser p("this;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_THIS);
})

// ---------------------------------------------------------------------------
// Binary expressions
// ---------------------------------------------------------------------------

TEST(compile_add, {
    auto bc = Compiler{}.compile(*Parser("1+2;").parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_NUMBER);
    ASSERT_EQ(bc->instructions[1].op, Opcode::PUSH_NUMBER);
    ASSERT_EQ(bc->instructions[2].op, Opcode::ADD);
})

TEST(compile_binary_mul, {
    auto bc = Compiler{}.compile(*Parser("3*4;").parse_program());
    ASSERT(instruction_index(*bc, Opcode::MUL) < bc->instructions.size());
})

TEST(compile_compound, {
    auto bc = Compiler{}.compile(*Parser("a = b + 1;").parse_program());
    ASSERT(bc->instructions.size() > 2);
})

// ---------------------------------------------------------------------------
// Call expressions
// ---------------------------------------------------------------------------

TEST(compile_call, {
    auto bc = Compiler{}.compile(*Parser("f(1,2);").parse_program());
    u32 last = instruction_index(*bc, Opcode::CALL);
    ASSERT(last != UINT32_MAX);
    ASSERT(instruction_index(*bc, Opcode::RETURN) > last);
})

// ---------------------------------------------------------------------------
// Array and Object literals
// ---------------------------------------------------------------------------

TEST(compile_array, {
    auto bc = Compiler{}.compile(*Parser("[1,2,3];").parse_program());
    ASSERT(instruction_index(*bc, Opcode::NEW_ARRAY) < bc->instructions.size());
})

TEST(compile_array_first_element, {
    auto bc = Compiler{}.compile(*Parser("[1,2,3];").parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_NUMBER);
})

TEST(compile_object, {
    auto bc = Compiler{}.compile(*Parser("var x = {a:1};").parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::NEW_OBJECT);
})

// ---------------------------------------------------------------------------
// Variable declarations
// ---------------------------------------------------------------------------

TEST(compile_var_decl, {
    auto bc = Compiler{}.compile(*Parser("var x = 42;").parse_program());
    ASSERT(bc->num_locals > 0);
})

TEST(compile_block, {
    auto bc = Compiler{}.compile(*Parser("{ var a = 1; var b = 2; }").parse_program());
    ASSERT_EQ(bc->num_locals, 2u);
})

// ---------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------

TEST(compile_if_else, {
    auto bc = Compiler{}.compile(*Parser("if (1) { 2; } else { 3; }").parse_program());
    ASSERT(has_jump_opcode(*bc, Opcode::JMP_IF_FALSE));
    ASSERT(has_jump_opcode(*bc, Opcode::JMP));
})

TEST(compile_while, {
    auto bc = Compiler{}.compile(*Parser("while(x) { y; }").parse_program());
    ASSERT(has_jump_opcode(*bc, Opcode::JMP_IF_FALSE));
})

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

TEST(compile_function_decl, {
    auto bc = Compiler{}.compile(*Parser("function f(x){return x+1;}").parse_program());
    ASSERT(!bc->child_functions.empty());
    ASSERT_EQ(bc->child_functions[0]->num_params, 1u);
    ASSERT_EQ(bc->child_functions[0]->name, "f");
})

TEST(compile_arrow, {
    auto bc = Compiler{}.compile(*Parser("const f = (x) => x + 1;").parse_program());
    ASSERT(!bc->child_functions.empty());
})

// ---------------------------------------------------------------------------
// Constant deduplication
// ---------------------------------------------------------------------------

TEST(constant_dedup, {
    Parser p("1 + 1;");
    auto bc = Compiler{}.compile(*p.parse_program());
    ASSERT_EQ(bc->constants.size(), 1u);
})

// ---------------------------------------------------------------------------
// Unary expressions
// ---------------------------------------------------------------------------

TEST(compile_unary_not, {
    auto bc = Compiler{}.compile(*Parser("!true;").parse_program());
    ASSERT(instruction_index(*bc, Opcode::NOT) < bc->instructions.size());
})

TEST(compile_unary_negate, {
    auto bc = Compiler{}.compile(*Parser("-42;").parse_program());
    ASSERT(instruction_index(*bc, Opcode::NEGATE) < bc->instructions.size());
})

TEST(compile_unary_typeof, {
    auto bc = Compiler{}.compile(*Parser("typeof x;").parse_program());
    ASSERT(instruction_index(*bc, Opcode::TYPEOF) < bc->instructions.size());
})

// ---------------------------------------------------------------------------
// Return statement
// ---------------------------------------------------------------------------

TEST(compile_return, {
    auto bc = Compiler{}.compile(*Parser("function f(){return 1;}").parse_program());
    ASSERT(!bc->child_functions.empty());
    ASSERT(instruction_index(*bc->child_functions[0], Opcode::RETURN) < bc->child_functions[0]->instructions.size());
})

// ---------------------------------------------------------------------------
// Throw
// ---------------------------------------------------------------------------

TEST(compile_throw, {
    auto bc = Compiler{}.compile(*Parser("throw 'err';").parse_program());
    ASSERT(instruction_index(*bc, Opcode::THROW) < bc->instructions.size());
})

// ---------------------------------------------------------------------------
// Template literal
// ---------------------------------------------------------------------------

TEST(compile_template_simple, {
    auto bc = Compiler{}.compile(*Parser("`hello`;").parse_program());
    ASSERT_EQ(bc->instructions[0].op, Opcode::PUSH_STRING);
})

TEST(compile_template_interpolated, {
    auto bc = Compiler{}.compile(*Parser("`a${x}b`;").parse_program());
    ASSERT(instruction_index(*bc, Opcode::TEMPLATE_LITERAL) < bc->instructions.size());
})

// ---------------------------------------------------------------------------
// Member access
// ---------------------------------------------------------------------------

TEST(compile_member_dot, {
    auto bc = Compiler{}.compile(*Parser("a.b;").parse_program());
    ASSERT(instruction_index(*bc, Opcode::GET_PROP) < bc->instructions.size());
})

TEST(compile_member_computed, {
    auto bc = Compiler{}.compile(*Parser("a[0];").parse_program());
    ASSERT(instruction_index(*bc, Opcode::GET_PROP_COMPUTED) < bc->instructions.size());
})

// ---------------------------------------------------------------------------
// Assignment
// ---------------------------------------------------------------------------

TEST(compile_assign_local, {
    auto bc = Compiler{}.compile(*Parser("var x; x = 5;").parse_program());
    ASSERT(instruction_index(*bc, Opcode::STORE_LOCAL) < bc->instructions.size());
})

TEST(compile_assign_global, {
    auto bc = Compiler{}.compile(*Parser("x = 5;").parse_program());
    ASSERT(instruction_index(*bc, Opcode::STORE_GLOBAL) < bc->instructions.size());
})
