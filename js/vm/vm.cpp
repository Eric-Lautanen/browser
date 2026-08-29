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
        // J-M1: constructor instances live only in frame slots until RETURN
        // pushes them; without frame roots any allocation-triggered collection
        // during the constructor body sweeps the object being constructed.
        for (auto &f : frames_) {
            roots.push_back(&f.this_value);
            roots.push_back(&f.new_object);
            // J-C5: boxes for captured locals hold the shared value; their
            // JSValue must be considered live while the frame exists.
            for (auto *b : f.local_boxes) {
                if (b) roots.push_back(&b->value);
            }
            if (f.closure_fn) {
                for (auto *b : f.closure_fn->closure) {
                    if (b) roots.push_back(&b->value);
                }
            }
        }
        for (auto &provider : gc_root_providers_) {
            auto extra = provider();
            roots.insert(roots.end(), extra.begin(), extra.end());
        }
        return roots;
    }

    GCBox *VM::ensure_box_for_slot(CallFrame &frame, u32 slot) {
        if (slot >= frame.local_boxes.size())
            return nullptr;
        if (!frame.local_boxes[slot]) {
            GCBox *box = heap_->alloc_box();
            box->value = stack_[frame.base + 1 + slot];
            frame.local_boxes[slot] = box;
        }
        return frame.local_boxes[slot];
    }

    JSValue &VM::local(u32 slot) {
        auto &f = frames_.back();
        if (slot < f.local_boxes.size() && f.local_boxes[slot])
            return f.local_boxes[slot]->value;
        return stack_[f.base + 1 + slot];
    }

    GCBox *VM::find_box_for_name(const std::string &name) {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            if (it->closure_fn) {
                auto *bc = it->closure_fn->bytecode;
                for (size_t i = 0; i < bc->captures.size(); ++i) {
                    if (bc->captures[i].name == name) {
                        return it->closure_fn->closure[i];
                    }
                }
            }
        }
        return nullptr;
    }

    void VM::add_gc_root_provider(std::function<std::vector<JSValue *>()> provider) {
        gc_root_providers_.push_back(std::move(provider));
    }

    JSValue VM::invoke(const JSValue &fn_val, const std::vector<JSValue> &args, JSValue this_val) {
        if (fn_val.type != JSValue::Type::FUNCTION || !fn_val.function_val)
            return JSValue::undefined();
        JSFunction *fn = fn_val.function_val;
        if (fn->native_fn) {
            std::vector<JSValue> full_args;
            full_args.push_back(this_val);
            for (const auto &a : args) full_args.push_back(a);
            NativeCallScope gc_guard(*this);
            return fn->native_fn(full_args, fn->native_context);
        }
        if (!fn->bytecode)
            return JSValue::undefined();

        VMState saved = save_state();
        size_t depth = frames_.size();
        size_t stack_base = stack_.size();

        push(this_val);  // callee/receiver slot
        for (const auto &a : args) push(a);
        auto *frame = push_call_frame(fn, static_cast<u32>(args.size()));
        frame->this_value = this_val;

        run_until_frames(depth);

        // A throw inside the callee unwinds past our boundary — restore the
        // caller's exact state and swallow the error (spec: rejected reaction).
        if (frames_.size() < depth || stack_.size() <= stack_base) {
            restore_state(std::move(saved));
            return JSValue::undefined();
        }
        JSValue result = pop();
        return result;
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
        frame.closure_fn = fn;
        frame.local_boxes.assign(total_slots, nullptr);
        frames_.push_back(std::move(frame));
        return &frames_.back();
    }

    void VM::pop_frame() {
        auto &frame = frames_.back();
        stack_.resize(frame.base);
        frames_.pop_back();
    }

    void VM::maybe_gc() {
        if (native_depth_ > 0)
            return;
        if (heap_->allocated_bytes() > heap_->threshold()) {
            std::vector<GCBox *> box_roots;
            for (auto &f : frames_) {
                for (auto *b : f.local_boxes)
                    if (b) box_roots.push_back(b);
                if (f.closure_fn) {
                    for (auto *b : f.closure_fn->closure)
                        if (b) box_roots.push_back(b);
                }
            }
            heap_->collect(gc_roots(), box_roots);
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
        CallFrame entry_frame;
        entry_frame.function = func;
        entry_frame.local_count = func->num_locals;
        entry_frame.this_value = JSValue::undefined();
        entry_frame.new_object = JSValue::undefined();
        entry_frame.local_boxes.assign(func->num_locals, nullptr);
        entry_frame.closure_fn = nullptr;
        frames_.push_back(std::move(entry_frame));
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
        run_until_frames(0);
        return stack_.empty() ? JSValue::undefined() : peek();
    }

    void VM::run_until_frames(size_t target_depth) {
        while (frames_.size() > target_depth) {
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
                case Opcode::PUSH_REGEX: {
                    // Regex literal: constant is "/pattern/flags". Build the
                    // object through the global RegExp constructor.
                    // invoke() prepends its own `this` arg, so pass only the
                    // real constructor args here (pattern, flags). Passing an
                    // extra placeholder undefined shifts the args by one and
                    // makes the constructor treat `undefined` as the source.
                    u32 idx = std::get<u32>(instr.operand);
                    std::string text = func->constants[idx].str;
                    if (text.size() >= 2 && text[0] == '/') {
                        size_t close = text.rfind('/');
                        std::string pattern = close > 0 ? text.substr(1, close - 1) : text.substr(1);
                        std::string flags = close + 1 < text.size() ? text.substr(close + 1) : "";
                        JSValue ctor = global_object()->get("RegExp");
                        if (ctor.type == JSValue::Type::FUNCTION && ctor.function_val) {
                            std::vector<JSValue> ctor_args = {JSValue::string(pattern), JSValue::string(flags)};
                            push(invoke(ctor, ctor_args, JSValue::undefined()));
                        } else {
                            push(JSValue::string(text));
                        }
                    } else {
                        push(JSValue::string(text));
                    }
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
                case Opcode::LOAD_CLOSURE: {
                    u32 idx = std::get<u32>(instr.operand);
                    op_load_closure(idx);
                    break;
                }
                case Opcode::STORE_CLOSURE: {
                    u32 idx = std::get<u32>(instr.operand);
                    op_store_closure(idx);
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
                    if (frames_.empty() || frames_.size() == target_depth) {
                        // Result of the entry frame: leave it on the stack for
                        // the caller (run()/invoke()) to pick up.
                        if (new_obj.type == JSValue::Type::OBJECT && ret.type != JSValue::Type::OBJECT &&
                            ret.type != JSValue::Type::FUNCTION) {
                            push(new_obj);
                        } else {
                            push(ret);
                        }
                        break;
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
                case Opcode::DELETE_PROP:
                    op_delete_prop(std::get<std::string>(instr.operand));
                    break;
                case Opcode::DELETE_PROP_COMPUTED:
                    op_delete_prop_computed();
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
                case Opcode::VOID_OP:
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
                    // J-C3: push onto the per-frame handler stack.
                    frames_.back().handlers.push_back(std::get<u32>(instr.operand));
                    break;
                }
                case Opcode::CATCH:
                    break;
                case Opcode::END_TRY: {
                    if (!frames_.back().handlers.empty())
                        frames_.back().handlers.pop_back();
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
    }

}  // namespace browser::js
