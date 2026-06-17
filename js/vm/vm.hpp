#pragma once
#include "../../tests/utility.hpp"
#include "../bytecode.hpp"
#include "../jit.hpp"
#include "../value.hpp"
#include "../../net/csp.hpp"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace browser::js {

    struct GCJSObject;
    class GCHeap;

    struct CallFrame {
        BytecodeFunction *function = nullptr;
        u32 ip = 0;
        u32 base = 0;
        u32 local_count = 0;
        u32 try_catch_ip = 0;
        JSValue this_value;
        JSValue new_object;
    };

    class VM {
    public:
        VM();
        ~VM();
        JSValue execute(BytecodeFunction *function);
        void push(const JSValue &val);
        JSValue pop();
        JSValue peek() const;
        JSObject *global_object();
        void register_builtins();
        class GCHeap *heap() { return heap_.get(); }
        struct VMState {
            std::vector<JSValue> stack;
            std::vector<CallFrame> frames;
            JSValue thrown_value;
        };
        std::vector<JSValue *> gc_roots();
        void add_gc_root_provider(std::function<std::vector<JSValue *>()> provider);
        JSFunction *create_native_fn(JSFunction::NativeFn fn, bool is_constructor = false, void *context = nullptr);
        JSValue add(const JSValue &a, const JSValue &b);
        void maybe_gc();
        CallFrame *push_call_frame(JSFunction *fn, u32 argc);
        VMState save_state() const;
        void restore_state(VMState &&state);

    JITState jit_state_;
    net::CSPPolicy csp_policy_;

    void set_csp_policy(const net::CSPPolicy& policy) { csp_policy_ = policy; }
    const net::CSPPolicy& csp_policy() const { return csp_policy_; }
    bool csp_allows_eval() const {
        if (!csp_policy_.has_directive("script-src") && !csp_policy_.has_directive("default-src")) return true;
        return csp_policy_.allows_eval();
    }

private:
    std::vector<JSValue> stack_;
    std::vector<CallFrame> frames_;
    GCJSObject *global_ = nullptr;
    std::unique_ptr<class GCHeap> heap_;
    JSValue global_root_;
    JSValue thrown_value_;
    std::vector<std::function<std::vector<JSValue *>()>> gc_root_providers_;

        JSValue run();
        void pop_frame();
        JSValue &local(u32 slot) {
            auto &f = frames_.back();
            return stack_[f.base + 1 + slot];
        }
        JSValue strict_eq(const JSValue &a, const JSValue &b);
        JSValue loose_eq(const JSValue &a, const JSValue &b);

        // Extracted opcode helpers
        void op_add();
        void op_sub();
        void op_mul();
        void op_div();
        void op_mod();
        void op_eq();
        void op_neq();
        void op_strict_eq();
        void op_strict_neq();
        void op_lt();
        void op_gt();
        void op_lte();
        void op_gte();
        void op_get_prop(const std::string &prop);
        void op_get_prop_computed();
        void op_set_prop(const std::string &prop);
        void op_set_prop_computed();
        void op_typeof();
        void op_instanceof();
        void op_negate();
        void op_not();
        void op_void();
        void op_bitwise_not();
        void op_bitwise_and();
        void op_bitwise_or();
        void op_bitwise_xor();
        void op_and();
        void op_or();
        void op_template_literal(u32 quasi_count);
        void op_throw();
        void op_new_object();
        void op_new_array(u32 count);
        void op_define_prop();
        void op_call(u32 argc);
        void op_call_method(const Instruction::CallMethodInfo &info);
        void op_new(u32 argc);
        void op_push_function(u32 idx);
    };

}  // namespace browser::js
