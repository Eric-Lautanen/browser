#include "vm.hpp"
#include "gc.hpp"
#include <iostream>
#include <cmath>
#include <cstdlib>

namespace browser::js {

static std::string format_number(f64 val) {
    std::string s = std::to_string(val);
    auto dot = s.find('.');
    if (dot != std::string::npos) {
        auto end = s.find_last_not_of('0');
        if (end == dot) end--;
        s = s.substr(0, end + 1);
    }
    return s;
}

bool JSValue::is_truthy() const {
    switch (type) {
        case Type::UNDEFINED: return false;
        case Type::NULL_VAL: return false;
        case Type::BOOLEAN: return bool_val;
        case Type::NUMBER: return number_val != 0 && !std::isnan(number_val);
        case Type::STRING: return !string_val.empty();
        case Type::OBJECT: return true;
        case Type::FUNCTION: return true;
        default: return true;
    }
}

std::string JSValue::to_string() const {
    switch (type) {
        case Type::NUMBER: return format_number(number_val);
        case Type::BOOLEAN: return bool_val ? "true" : "false";
        case Type::STRING: return string_val;
        case Type::UNDEFINED: return "undefined";
        case Type::NULL_VAL: return "null";
        case Type::OBJECT: return "[object Object]";
        case Type::FUNCTION: return "[object Object]";
        default: return "";
    }
}

f64 JSValue::to_number() const {
    switch (type) {
        case Type::NUMBER: return number_val;
        case Type::STRING: return std::strtod(string_val.c_str(), nullptr);
        case Type::BOOLEAN: return bool_val ? 1.0 : 0.0;
        case Type::UNDEFINED: return NAN;
        case Type::NULL_VAL: return 0.0;
        case Type::OBJECT: return NAN;
        case Type::FUNCTION: return NAN;
        default: return 0;
    }
}

JSValue JSObject::get(const std::string& name) const {
    auto it = properties.find(name);
    if (it != properties.end()) return it->second;
    return JSValue::undefined();
}

void JSObject::set(const std::string& name, const JSValue& val) {
    properties[name] = val;
}

VM::VM() {
    heap_ = std::make_unique<GCHeap>();
    global_ = heap_->alloc_object();
    global_root_ = JSValue::object(&global_->obj);
}

VM::~VM() = default;

void VM::push(const JSValue& val) {
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

JSObject* VM::global_object() {
    return &global_->obj;
}

std::vector<JSValue*> VM::gc_roots() {
    std::vector<JSValue*> roots;
    for (auto& v : stack_) roots.push_back(&v);
    roots.push_back(&global_root_);
    if (thrown_value_.type != JSValue::Type::UNDEFINED)
        roots.push_back(&thrown_value_);
    for (auto& provider : gc_root_providers_) {
        auto extra = provider();
        roots.insert(roots.end(), extra.begin(), extra.end());
    }
    return roots;
}

void VM::add_gc_root_provider(std::function<std::vector<JSValue*>()> provider) {
    gc_root_providers_.push_back(std::move(provider));
}

VM::VMState VM::save_state() const {
    return {stack_, frames_, thrown_value_};
}

void VM::restore_state(VMState&& state) {
    stack_ = std::move(state.stack);
    frames_ = std::move(state.frames);
    thrown_value_ = state.thrown_value;
}

JSFunction* VM::create_native_fn(JSFunction::NativeFn fn, bool is_constructor, void* context) {
    auto* f = heap_->alloc_function();
    f->fn.native_fn = fn;
    f->fn.is_constructor = is_constructor;
    f->fn.native_context = context;
    return &f->fn;
}

void VM::push_call_frame(JSFunction* fn, u32 argc) {
    auto* bc = fn->bytecode;
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
}

void VM::pop_frame() {
    auto& frame = frames_.back();
    stack_.resize(frame.base);
    frames_.pop_back();
}

void VM::maybe_gc() {
    if (heap_->allocated_bytes() > heap_->threshold()) {
        heap_->collect(gc_roots());
    }
}

JSValue VM::add(const JSValue& a, const JSValue& b) {
    if (a.type == JSValue::Type::STRING || b.type == JSValue::Type::STRING) {
        return JSValue::string(a.to_string() + b.to_string());
    }
    return JSValue::number(a.to_number() + b.to_number());
}

JSValue VM::strict_eq(const JSValue& a, const JSValue& b) {
    if (a.type != b.type) return JSValue::boolean(false);
    switch (a.type) {
        case JSValue::Type::NUMBER:
            if (std::isnan(a.number_val)) return JSValue::boolean(false);
            return JSValue::boolean(a.number_val == b.number_val);
        case JSValue::Type::STRING:   return JSValue::boolean(a.string_val == b.string_val);
        case JSValue::Type::BOOLEAN:  return JSValue::boolean(a.bool_val == b.bool_val);
        case JSValue::Type::OBJECT:   return JSValue::boolean(a.object_val == b.object_val);
        case JSValue::Type::FUNCTION: return JSValue::boolean(a.function_val == b.function_val);
        default: return JSValue::boolean(true);
    }
}

JSValue VM::loose_eq(const JSValue& a, const JSValue& b) {
    if (a.type == b.type) return strict_eq(a, b);
    if ((a.type == JSValue::Type::NULL_VAL && b.type == JSValue::Type::UNDEFINED) ||
        (a.type == JSValue::Type::UNDEFINED && b.type == JSValue::Type::NULL_VAL))
        return JSValue::boolean(true);
    if (a.type == JSValue::Type::STRING && b.type == JSValue::Type::NUMBER) return loose_eq(JSValue::number(a.to_number()), b);
    if (a.type == JSValue::Type::NUMBER && b.type == JSValue::Type::STRING) return loose_eq(a, JSValue::number(b.to_number()));
    if (a.type == JSValue::Type::BOOLEAN) return loose_eq(JSValue::number(a.to_number()), b);
    if (b.type == JSValue::Type::BOOLEAN) return loose_eq(a, JSValue::number(b.to_number()));
    return JSValue::boolean(false);
}

void VM::register_builtins() {
    auto* console_obj = heap_->alloc_object();
    console_obj->obj.set("log", JSValue::function(create_native_fn(
        [](const std::vector<JSValue>& args, void*) -> JSValue {
            for (auto& a : args) std::cout << a.to_string() << " ";
            std::cout << std::endl;
            return JSValue::undefined();
        }
    )));
    global_->obj.set("console", JSValue::object(&console_obj->obj));

    global_->obj.set("parseInt", JSValue::function(create_native_fn(
        [](const std::vector<JSValue>& args, void*) -> JSValue {
            if (args.empty()) return JSValue::number(NAN);
            return JSValue::number((f64)std::strtol(args[0].to_string().c_str(), nullptr, 10));
        }
    )));

    global_->obj.set("Array", JSValue::function(create_native_fn(
        [](const std::vector<JSValue>& args, void* context) -> JSValue {
            auto* vm = static_cast<VM*>(context);
            auto* arr = vm->heap()->alloc_object();
            arr->obj.is_array = true;
            for (auto& a : args) arr->obj.array_elements.push_back(a);
            return JSValue::object(&arr->obj);
        },
        true, this
    )));

    global_->obj.set("NaN", JSValue::number(NAN));
    global_->obj.set("undefined", JSValue::undefined());
}

JSValue VM::execute(BytecodeFunction* func) {
    if (jit_state_.jit_entries.count(func)) {
        auto fn = (void(*)(VM*))jit_state_.jit_entries[func];
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
    frames_.push_back({func, 0, 0, func->num_locals});
    for (u32 i = 0; i < func->num_locals; i++) {
        push(JSValue::undefined());
    }
    auto result = run();

    if (jit_state_.compiler) {
        jit_state_.call_counts[func]++;
        if (jit_state_.call_counts[func] == 100) {
            void* code = jit_state_.compiler->compile(func);
            if (code)
                jit_state_.jit_entries[func] = code;
        }
    }

    return result;
}

JSValue VM::run() {
    while (!frames_.empty()) {
        auto& frame = frames_.back();
        auto* func = frame.function;
        if (frame.ip >= func->instructions.size()) break;
        maybe_gc();
        auto& instr = func->instructions[frame.ip++];

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
            case Opcode::PUSH_THIS:
                push(JSValue::object(&global_->obj));
                break;
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
            case Opcode::ADD: {
                auto b = pop();
                auto a = pop();
                push(add(a, b));
                break;
            }
            case Opcode::SUB: {
                auto b = pop();
                auto a = pop();
                push(JSValue::number(a.to_number() - b.to_number()));
                break;
            }
            case Opcode::MUL: {
                auto b = pop();
                auto a = pop();
                push(JSValue::number(a.to_number() * b.to_number()));
                break;
            }
            case Opcode::DIV: {
                auto b = pop();
                auto a = pop();
                f64 denom = b.to_number();
                if (denom == 0) {
                    f64 num = a.to_number();
                    push(JSValue::number(num == 0 ? NAN : (num > 0 ? INFINITY : -INFINITY)));
                } else {
                    push(JSValue::number(a.to_number() / denom));
                }
                break;
            }
            case Opcode::MOD: {
                auto b = pop();
                auto a = pop();
                push(JSValue::number(std::fmod(a.to_number(), b.to_number())));
                break;
            }
            case Opcode::EQ: {
                auto b = pop();
                auto a = pop();
                push(loose_eq(a, b));
                break;
            }
            case Opcode::NEQ: {
                auto b = pop();
                auto a = pop();
                push(JSValue::boolean(!loose_eq(a, b).bool_val));
                break;
            }
            case Opcode::STRICT_EQ: {
                auto b = pop();
                auto a = pop();
                push(strict_eq(a, b));
                break;
            }
            case Opcode::STRICT_NEQ: {
                auto b = pop();
                auto a = pop();
                push(JSValue::boolean(!strict_eq(a, b).bool_val));
                break;
            }
            case Opcode::LT: {
                auto b = pop();
                auto a = pop();
                push(JSValue::boolean(a.to_number() < b.to_number()));
                break;
            }
            case Opcode::GT: {
                auto b = pop();
                auto a = pop();
                push(JSValue::boolean(a.to_number() > b.to_number()));
                break;
            }
            case Opcode::LTE: {
                auto b = pop();
                auto a = pop();
                push(JSValue::boolean(a.to_number() <= b.to_number()));
                break;
            }
            case Opcode::GTE: {
                auto b = pop();
                auto a = pop();
                push(JSValue::boolean(a.to_number() >= b.to_number()));
                break;
            }
            case Opcode::JMP: {
                u32 target = std::get<u32>(instr.operand);
                frame.ip = target;
                break;
            }
            case Opcode::JMP_IF_FALSE: {
                auto v = pop();
                u32 target = std::get<u32>(instr.operand);
                if (!v.is_truthy()) frame.ip = target;
                break;
            }
            case Opcode::JMP_IF_TRUE: {
                auto v = pop();
                u32 target = std::get<u32>(instr.operand);
                if (v.is_truthy()) frame.ip = target;
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
            case Opcode::CALL: {
                u32 argc = std::get<u32>(instr.operand);
                if (stack_.size() < argc + 1) { push(JSValue::undefined()); break; }
                auto callee = stack_[stack_.size() - 1 - argc];
                if (callee.type == JSValue::Type::FUNCTION) {
                    JSFunction* fn = callee.function_val;
                    if (fn->native_fn) {
                        std::vector<JSValue> args;
                        for (u32 i = 0; i < argc; i++) {
                            args.push_back(stack_[stack_.size() - argc + i]);
                        }
                        stack_.resize(stack_.size() - argc - 1);
                        push(fn->native_fn(args, fn->native_context));
                    } else if (fn->bytecode) {
                        push_call_frame(fn, argc);
                    } else {
                        push(JSValue::undefined());
                    }
                } else {
                    stack_.resize(stack_.size() - argc - 1);
                    push(JSValue::undefined());
                }
                break;
            }
            case Opcode::CALL_METHOD: {
                auto& info = std::get<Instruction::CallMethodInfo>(instr.operand);
                u32 argc = info.argc;
                bool is_computed = info.method_name.empty();
                u32 needed = argc + 1 + (is_computed ? 1 : 0);
                if (stack_.size() < needed) { push(JSValue::undefined()); break; }
                JSValue callee;
                auto resolve_method = [&]() {
                    u32 obj_idx = stack_.size() - 1 - argc;
                    if (is_computed) {
                        obj_idx--;
                    }
                    JSValue obj_val = stack_[obj_idx];
                    if (is_computed) {
                        JSValue key_val = stack_[obj_idx + 1];
                        if (obj_val.type == JSValue::Type::OBJECT) {
                            callee = obj_val.object_val->get(key_val.to_string());
                        } else {
                            callee = JSValue::undefined();
                        }
                        stack_.erase(stack_.begin() + obj_idx + 1);
                    } else {
                        if (obj_val.type == JSValue::Type::OBJECT) {
                            callee = obj_val.object_val->get(info.method_name);
                        } else {
                            callee = JSValue::undefined();
                        }
                    }
                    if (obj_idx < stack_.size()) {
                        stack_[obj_idx] = callee;
                    }
                };
                resolve_method();
                if (callee.type == JSValue::Type::FUNCTION) {
                    JSFunction* fn = callee.function_val;
                    if (fn->native_fn) {
                        std::vector<JSValue> args;
                        for (u32 i = 0; i < argc; i++) {
                            args.push_back(stack_[stack_.size() - argc + i]);
                        }
                        stack_.resize(stack_.size() - argc - 1);
                        push(fn->native_fn(args, fn->native_context));
                    } else if (fn->bytecode) {
                        push_call_frame(fn, argc);
                    } else {
                        push(JSValue::undefined());
                    }
                } else {
                    stack_.resize(stack_.size() - argc - 1);
                    push(JSValue::undefined());
                }
                break;
            }
            case Opcode::RETURN: {
                auto ret = pop();
                pop_frame();
                if (frames_.empty()) return ret;
                push(ret);
                break;
            }
            case Opcode::PUSH_FUNCTION: {
                u32 idx = std::get<u32>(instr.operand);
                auto* child_bc = func->child_functions[idx].get();
                auto* gc_fn = heap_->alloc_function();
                gc_fn->fn.bytecode = child_bc;
                gc_fn->fn.name = child_bc->name;
                push(JSValue::function(&gc_fn->fn));
                break;
            }
            case Opcode::NEW_OBJECT: {
                auto* obj = heap_->alloc_object();
                push(JSValue::object(&obj->obj));
                break;
            }
            case Opcode::DEFINE_PROP: {
                auto val = pop();
                auto key = pop();
                if (!stack_.empty() && stack_.back().type == JSValue::Type::OBJECT) {
                    stack_.back().object_val->set(key.to_string(), val);
                }
                break;
            }
            case Opcode::GET_PROP: {
                auto obj_val = pop();
                std::string prop = std::get<std::string>(instr.operand);
                if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val) {
                    push(obj_val.object_val->get(prop));
                } else {
                    push(JSValue::undefined());
                }
                break;
            }
            case Opcode::GET_PROP_COMPUTED: {
                auto key_val = pop();
                auto obj_val = pop();
                if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val) {
                    push(obj_val.object_val->get(key_val.to_string()));
                } else {
                    push(JSValue::undefined());
                }
                break;
            }
            case Opcode::SET_PROP: {
                auto val = pop();
                auto obj_val = pop();
                std::string prop = std::get<std::string>(instr.operand);
                if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val) {
                    obj_val.object_val->set(prop, val);
                }
                push(val);
                break;
            }
            case Opcode::SET_PROP_COMPUTED: {
                auto val = pop();
                auto key_val = pop();
                auto obj_val = pop();
                if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val) {
                    obj_val.object_val->set(key_val.to_string(), val);
                }
                push(val);
                break;
            }
            case Opcode::NEW_ARRAY: {
                u32 count = std::get<u32>(instr.operand);
                if (count > stack_.size()) { push(JSValue::undefined()); break; }
                auto* arr = heap_->alloc_object();
                arr->obj.is_array = true;
                u32 start = (u32)stack_.size() - count;
                for (u32 i = 0; i < count; i++) {
                    arr->obj.array_elements.push_back(stack_[start + i]);
                }
                stack_.resize(start);
                push(JSValue::object(&arr->obj));
                break;
            }
            case Opcode::NEGATE: {
                auto v = pop();
                push(JSValue::number(-v.to_number()));
                break;
            }
            case Opcode::NOT: {
                auto v = pop();
                push(JSValue::boolean(!v.is_truthy()));
                break;
            }
            case Opcode::TYPEOF: {
                auto v = pop();
                switch (v.type) {
                    case JSValue::Type::NUMBER:   push(JSValue::string("number")); break;
                    case JSValue::Type::STRING:   push(JSValue::string("string")); break;
                    case JSValue::Type::BOOLEAN:  push(JSValue::string("boolean")); break;
                    case JSValue::Type::UNDEFINED: push(JSValue::string("undefined")); break;
                    case JSValue::Type::NULL_VAL: push(JSValue::string("object")); break;
                    case JSValue::Type::OBJECT:   push(JSValue::string("object")); break;
                    case JSValue::Type::FUNCTION: push(JSValue::string("function")); break;
                    default: push(JSValue::string("undefined")); break;
                }
                break;
            }
            case Opcode::VOID:
                pop();
                push(JSValue::undefined());
                break;
            case Opcode::BITWISE_NOT: {
                auto v = pop();
                push(JSValue::number((f64)(~(i32)v.to_number())));
                break;
            }
            case Opcode::BITWISE_AND: {
                auto b = pop();
                auto a = pop();
                push(JSValue::number((f64)((i32)a.to_number() & (i32)b.to_number())));
                break;
            }
            case Opcode::BITWISE_OR: {
                auto b = pop();
                auto a = pop();
                push(JSValue::number((f64)((i32)a.to_number() | (i32)b.to_number())));
                break;
            }
            case Opcode::BITWISE_XOR: {
                auto b = pop();
                auto a = pop();
                push(JSValue::number((f64)((i32)a.to_number() ^ (i32)b.to_number())));
                break;
            }
            case Opcode::AND: {
                auto b = pop();
                auto a = pop();
                push(a.is_truthy() ? b : a);
                break;
            }
            case Opcode::OR: {
                auto b = pop();
                auto a = pop();
                push(a.is_truthy() ? a : b);
                break;
            }
            case Opcode::TEMPLATE_LITERAL: {
                u32 quasi_count = std::get<u32>(instr.operand);
                u32 total = 2 * quasi_count - 1;
                std::vector<JSValue> items;
                for (u32 i = 0; i < total; i++) {
                    items.push_back(pop());
                }
                std::string result;
                for (u32 i = 0; i < total; i++) {
                    result += items[total - 1 - i].to_string();
                }
                push(JSValue::string(result));
                break;
            }
            case Opcode::THROW: {
                auto val = pop();
                thrown_value_ = val;
                while (!frames_.empty()) {
                    auto& f = frames_.back();
                    if (f.try_catch_ip > 0) {
                        f.ip = f.try_catch_ip;
                        f.try_catch_ip = 0;
                        push(val);
                        thrown_value_ = JSValue::undefined();
                        break;
                    }
                    pop_frame();
                }
                if (frames_.empty()) thrown_value_ = JSValue::undefined();
                break;
            }
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
            case Opcode::NEW:
            case Opcode::OPCODE_COUNT:
                push(JSValue::undefined());
                break;
        }
    }
    if (stack_.empty()) return JSValue::undefined();
    return pop();
}

extern "C" {

void jit_push_number(VM* vm, u64 val_bits) {
    f64 val;
    std::memcpy(&val, &val_bits, sizeof(val));
    vm->push(JSValue::number(val));
}

void jit_push_undefined(VM* vm) {
    vm->push(JSValue::undefined());
}

void jit_pop(VM* vm) {
    vm->pop();
}

void jit_call_add(VM* vm) {
    auto b = vm->pop();
    auto a = vm->pop();
    vm->push(vm->add(a, b));
}

void jit_call_sub(VM* vm) {
    auto b = vm->pop();
    auto a = vm->pop();
    vm->push(JSValue::number(a.to_number() - b.to_number()));
}

void jit_call_mul(VM* vm) {
    auto b = vm->pop();
    auto a = vm->pop();
    vm->push(JSValue::number(a.to_number() * b.to_number()));
}

void jit_call_div(VM* vm) {
    auto b = vm->pop();
    auto a = vm->pop();
    f64 denom = b.to_number();
    if (denom == 0) {
        f64 num = a.to_number();
        vm->push(JSValue::number(num == 0 ? NAN : (num > 0 ? INFINITY : -INFINITY)));
    } else {
        vm->push(JSValue::number(a.to_number() / denom));
    }
}

void jit_call_mod(VM* vm) {
    auto b = vm->pop();
    auto a = vm->pop();
    vm->push(JSValue::number(std::fmod(a.to_number(), b.to_number())));
}

u32 jit_pop_and_is_truthy(VM* vm) {
    auto v = vm->pop();
    return v.is_truthy() ? 1 : 0;
}

}

} // namespace browser::js
