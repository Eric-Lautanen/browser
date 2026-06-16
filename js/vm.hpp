#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include "value.hpp"
#include "bytecode.hpp"
#include "jit.hpp"
#include "../tests/utility.hpp"

namespace browser::js {

struct GCJSObject;
class GCHeap;

struct CallFrame {
    BytecodeFunction* function = nullptr;
    u32 ip = 0;
    u32 base = 0;
    u32 local_count = 0;
    u32 try_catch_ip = 0;
};

class VM {
public:
    VM();
    ~VM();
    JSValue execute(BytecodeFunction* function);
    void push(const JSValue& val);
    JSValue pop();
    JSValue peek() const;
    JSObject* global_object();
    void register_builtins();
    class GCHeap* heap() { return heap_.get(); }
    struct VMState {
        std::vector<JSValue> stack;
        std::vector<CallFrame> frames;
        JSValue thrown_value;
    };
    std::vector<JSValue*> gc_roots();
    void add_gc_root_provider(std::function<std::vector<JSValue*>()> provider);
    JSFunction* create_native_fn(JSFunction::NativeFn fn, bool is_constructor = false, void* context = nullptr);
    JSValue add(const JSValue& a, const JSValue& b);
    void maybe_gc();
    void push_call_frame(JSFunction* fn, u32 argc);
    VMState save_state() const;
    void restore_state(VMState&& state);

    JITState jit_state_;

private:
    std::vector<JSValue> stack_;
    std::vector<CallFrame> frames_;
    GCJSObject* global_ = nullptr;
    std::unique_ptr<class GCHeap> heap_;
    JSValue global_root_;
    JSValue thrown_value_;
    std::vector<std::function<std::vector<JSValue*>()>> gc_root_providers_;

    JSValue run();
    void pop_frame();
    JSValue& local(u32 slot) {
        auto& f = frames_.back();
        return stack_[f.base + 1 + slot];
    }
    JSValue strict_eq(const JSValue& a, const JSValue& b);
    JSValue loose_eq(const JSValue& a, const JSValue& b);
};

} // namespace browser::js
