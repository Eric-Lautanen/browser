#include "vm.hpp"

#include "gc.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace browser::js {

    VM::VM() {
        heap_ = std::make_unique<GCHeap>();
        global_ = heap_->alloc_object();
        global_root_ = JSValue::object(&global_->obj);
    }

    VM::~VM() = default;

    void VM::push(const JSValue &val) {
        stack_.push_back(val);
    }

    JSValue VM::pop() {
        auto v = stack_.back();
        stack_.pop_back();
        return v;
    }

    JSValue VM::peek() const {
        return stack_.back();
    }

    JSObject *VM::global_object() {
        return &global_->obj;
    }

    std::vector<JSValue *> VM::gc_roots() {
        std::vector<JSValue *> roots;
        for (auto &v : stack_) roots.push_back(&v);
        roots.push_back(&global_root_);
        if (thrown_value_.type != JSValue::Type::UNDEFINED)
            roots.push_back(&thrown_value_);
        for (auto &provider : gc_root_providers_) {
            auto extra = provider();
            roots.insert(roots.end(), extra.begin(), extra.end());
        }
        return roots;
    }

    void VM::add_gc_root_provider(std::function<std::vector<JSValue *>()> provider) {
        gc_root_providers_.push_back(std::move(provider));
    }

    VM::VMState VM::save_state() const {
        return {stack_, frames_, thrown_value_};
    }

    void VM::restore_state(VMState &&state) {
        stack_ = std::move(state.stack);
        frames_ = std::move(state.frames);
        thrown_value_ = state.thrown_value;
    }

    CallFrame *VM::push_call_frame(JSFunction *fn, u32 argc) {
        auto *bc = fn->bytecode;
        CallFrame frame;
        frame.base = (u32)stack_.size() - argc - 1;
        frame.local_count = bc->num_locals;
        u32 total_slots = bc->num_locals;
        for (u32 i = argc; i < total_slots; i++) {
            push(JSValue::undefined());
        }
        frame.ip = 0;
        frame.function = bc;
        frames_.push_back(frame);
        return &frames_.back();
    }

    void VM::pop_frame() {
        auto &frame = frames_.back();
        stack_.resize(frame.base);
        frames_.pop_back();
    }

    void VM::maybe_gc() {
        if (heap_->allocated_bytes() > heap_->threshold()) {
            heap_->collect(gc_roots());
        }
    }

    JSValue VM::execute(BytecodeFunction *func) {
        if (csp_policy_.has_directive("script-src") || csp_policy_.has_directive("default-src")) {
            if (!csp_policy_.allows_inline_script()) {
                return JSValue::undefined();
            }
        }

        if (jit_state_.jit_entries.count(func)) {
            auto fn = (void (*)(VM *))jit_state_.jit_entries[func];
            stack_.clear();
            frames_.clear();
            maybe_gc();
            fn(this);
            auto result = stack_.empty() ? JSValue::undefined() : pop();
            return result;
        }

        stack_.clear();
        frames_.clear();
        push(JSValue::undefined());
        frames_.push_back({func, 0, 0, func->num_locals, 0, JSValue::undefined(), JSValue::undefined()});
        for (u32 i = 0; i < func->num_locals; i++) {
            push(JSValue::undefined());
        }
        auto result = run();

        if (jit_state_.compiler) {
            jit_state_.call_counts[func]++;
            if (jit_state_.call_counts[func] == 100) {
                void *code = jit_state_.compiler->compile(func);
                if (code)
                    jit_state_.jit_entries[func] = code;
            }
        }

        return result;
    }

    JSValue VM::run() {
        while (!frames_.empty()) {
            auto &frame = frames_.back();
            auto *func = frame.function;
            if (frame.ip >= func->instructions.size())
                break;
            maybe_gc();
            auto &instr = func->instructions[frame.ip++];

            switch (instr.op) {
                case Opcode::PUSH_NUMBER: {
                    u32 idx = std::get<u32>(instr.operand);
                    push(JSValue::number(func->constants[idx].number));
                    break;
                }
                case Opcode::PUSH_STRING: {
                    u32 idx = std::get<u32>(instr.operand);
                    push(JSValue::string(func->constants[idx].str));
                    break;
                }
                case Opcode::PUSH_BOOL: {
                    u32 idx = std::get<u32>(instr.operand);
                    push(JSValue::boolean(func->constants[idx].boolean));
                    break;
                }
                case Opcode::PUSH_NULL:
                    push(JSValue::null());
                    break;
                case Opcode::PUSH_UNDEFINED:
                    push(JSValue::undefined());
                    break;
                case Opcode::PUSH_THIS: {
                    JSValue this_val = JSValue::object(&global_->obj);
                    if (!frames_.empty() && frames_.back().this_value.type != JSValue::Type::UNDEFINED) {
                        this_val = frames_.back().this_value;
                    }
                    push(this_val);
                    break;
                }
                case Opcode::POP:
                    pop();
                    break;
                case Opcode::DUP: {
                    auto v = peek();
                    push(v);
                    break;
                }
                case Opcode::SWAP: {
                    auto a = pop();
                    auto b = pop();
                    push(a);
                    push(b);
                    break;
                }
                case Opcode::LOAD_LOCAL: {
                    u32 slot = std::get<u32>(instr.operand);
                    push(local(slot));
                    break;
                }
                case Opcode::STORE_LOCAL: {
                    u32 slot = std::get<u32>(instr.operand);
                    local(slot) = stack_.back();
                    break;
                }
                case Opcode::LOAD_GLOBAL: {
                    std::string name = std::get<std::string>(instr.operand);
                    push(global_->obj.get(name));
                    break;
                }
                case Opcode::STORE_GLOBAL: {
                    std::string name = std::get<std::string>(instr.operand);
                    global_->obj.set(name, peek());
                    break;
                }
                case Opcode::ADD:
                    op_add();
                    break;
                case Opcode::SUB:
                    op_sub();
                    break;
                case Opcode::MUL:
                    op_mul();
                    break;
                case Opcode::DIV:
                    op_div();
                    break;
                case Opcode::MOD:
                    op_mod();
                    break;
                case Opcode::EQ:
                    op_eq();
                    break;
                case Opcode::NEQ:
                    op_neq();
                    break;
                case Opcode::STRICT_EQ:
                    op_strict_eq();
                    break;
                case Opcode::STRICT_NEQ:
                    op_strict_neq();
                    break;
                case Opcode::LT:
                    op_lt();
                    break;
                case Opcode::GT:
                    op_gt();
                    break;
                case Opcode::LTE:
                    op_lte();
                    break;
                case Opcode::GTE:
                    op_gte();
                    break;
                case Opcode::JMP: {
                    u32 target = std::get<u32>(instr.operand);
                    frame.ip = target;
                    break;
                }
                case Opcode::JMP_IF_FALSE: {
                    auto v = pop();
                    u32 target = std::get<u32>(instr.operand);
                    if (!v.is_truthy())
                        frame.ip = target;
                    break;
                }
                case Opcode::JMP_IF_TRUE: {
                    auto v = pop();
                    u32 target = std::get<u32>(instr.operand);
                    if (v.is_truthy())
                        frame.ip = target;
                    break;
                }
                case Opcode::JMP_IF_NULLISH: {
                    auto v = pop();
                    u32 target = std::get<u32>(instr.operand);
                    if (v.type == JSValue::Type::NULL_VAL || v.type == JSValue::Type::UNDEFINED) {
                        frame.ip = target;
                    }
                    break;
                }
                case Opcode::CALL:
                    op_call(std::get<u32>(instr.operand));
                    break;
                case Opcode::CALL_METHOD:
                    op_call_method(std::get<Instruction::CallMethodInfo>(instr.operand));
                    break;
                case Opcode::NEW:
                    op_new(std::get<u32>(instr.operand));
                    break;
                case Opcode::INSTANCEOF:
                    op_instanceof();
                    break;
                case Opcode::RETURN: {
                    auto ret = pop();
                    JSValue new_obj = frames_.back().new_object;
                    pop_frame();
                    if (frames_.empty()) {
                        if (new_obj.type == JSValue::Type::OBJECT && ret.type != JSValue::Type::OBJECT &&
                            ret.type != JSValue::Type::FUNCTION) {
                            return new_obj;
                        }
                        return ret;
                    }
                    if (new_obj.type == JSValue::Type::OBJECT && ret.type != JSValue::Type::OBJECT &&
                        ret.type != JSValue::Type::FUNCTION) {
                        push(new_obj);
                    } else {
                        push(ret);
                    }
                    break;
                }
                case Opcode::PUSH_FUNCTION:
                    op_push_function(std::get<u32>(instr.operand));
                    break;
                case Opcode::NEW_OBJECT:
                    op_new_object();
                    break;
                case Opcode::DEFINE_PROP:
                    op_define_prop();
                    break;
                case Opcode::GET_PROP:
                    op_get_prop(std::get<std::string>(instr.operand));
                    break;
                case Opcode::GET_PROP_COMPUTED:
                    op_get_prop_computed();
                    break;
                case Opcode::SET_PROP:
                    op_set_prop(std::get<std::string>(instr.operand));
                    break;
                case Opcode::SET_PROP_COMPUTED:
                    op_set_prop_computed();
                    break;
                case Opcode::NEW_ARRAY:
                    op_new_array(std::get<u32>(instr.operand));
                    break;
                case Opcode::NEGATE:
                    op_negate();
                    break;
                case Opcode::NOT:
                    op_not();
                    break;
                case Opcode::TYPEOF:
                    op_typeof();
                    break;
                case Opcode::VOID:
                    op_void();
                    break;
                case Opcode::BITWISE_NOT:
                    op_bitwise_not();
                    break;
                case Opcode::BITWISE_AND:
                    op_bitwise_and();
                    break;
                case Opcode::BITWISE_OR:
                    op_bitwise_or();
                    break;
                case Opcode::BITWISE_XOR:
                    op_bitwise_xor();
                    break;
                case Opcode::AND:
                    op_and();
                    break;
                case Opcode::OR:
                    op_or();
                    break;
                case Opcode::TEMPLATE_LITERAL:
                    op_template_literal(std::get<u32>(instr.operand));
                    break;
                case Opcode::THROW:
                    op_throw();
                    break;
                case Opcode::TRY: {
                    frames_.back().try_catch_ip = std::get<u32>(instr.operand);
                    break;
                }
                case Opcode::CATCH:
                    break;
                case Opcode::END_TRY: {
                    frames_.back().try_catch_ip = 0;
                    break;
                }
                case Opcode::YIELD:
                    push(JSValue::undefined());
                    break;
                case Opcode::NOP:
                    break;
                case Opcode::LOAD_VAR:
                case Opcode::STORE_VAR:
                case Opcode::OPCODE_COUNT:
                    push(JSValue::undefined());
                    break;
            }
        }
        if (stack_.empty())
            return JSValue::undefined();
        return pop();
    }

}  // namespace browser::js
