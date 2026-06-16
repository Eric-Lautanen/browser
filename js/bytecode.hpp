#pragma once
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include "../tests/utility.hpp"

namespace browser::js {

enum class Opcode : u8 {
    PUSH_NULL, PUSH_UNDEFINED, PUSH_BOOL, PUSH_NUMBER, PUSH_STRING, PUSH_THIS,
    DUP, POP, SWAP,
    LOAD_VAR, STORE_VAR, LOAD_GLOBAL, STORE_GLOBAL, LOAD_LOCAL, STORE_LOCAL,
    NEW_ARRAY, NEW_OBJECT, GET_PROP, GET_PROP_COMPUTED, SET_PROP, SET_PROP_COMPUTED,
    NEGATE, NOT, TYPEOF, VOID, BITWISE_NOT,
    ADD, SUB, MUL, DIV, MOD,
    EQ, NEQ, STRICT_EQ, STRICT_NEQ, LT, GT, LTE, GTE,
    AND, OR, BITWISE_AND, BITWISE_OR, BITWISE_XOR,
    JMP, JMP_IF_TRUE, JMP_IF_FALSE, JMP_IF_NULLISH,
    CALL, CALL_METHOD, NEW, RETURN, YIELD,
    DEFINE_PROP, THROW, TRY, CATCH, END_TRY,
    TEMPLATE_LITERAL, NOP, PUSH_FUNCTION, OPCODE_COUNT
};

struct Instruction {
    Opcode op;
    struct CallMethodInfo { std::string method_name; u32 argc; };
    std::variant<f64, u32, std::string, CallMethodInfo> operand;
};

struct BytecodeFunction {
    std::string name;
    u32 num_locals = 0, num_params = 0;
    std::vector<Instruction> instructions;
    struct Constant {
        enum class Type { NUMBER, STRING, BOOL };
        Type type;
        f64 number = 0;
        std::string str;
        bool boolean = false;
    };
    std::vector<Constant> constants;
    std::vector<std::unique_ptr<BytecodeFunction>> child_functions;

    u32 add_constant(const Constant& c);
    void emit(Opcode op);
    void emit(Opcode op, u32 operand);
    void emit(Opcode op, f64 operand);
    void emit(Opcode op, const std::string& operand);
    void emit(Opcode op, const Instruction::CallMethodInfo& info);
};

} // namespace browser::js
