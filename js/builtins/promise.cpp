#include "builtins.hpp"
#include <vector>
#include <functional>

namespace browser::js::builtins {

struct PromiseCtx { VM* vm; };

struct PromiseData {
    enum State { PENDING, FULFILLED, REJECTED };
    State state = PENDING;
    JSValue result;
    std::vector<std::pair<JSValue, JSValue>> then_callbacks; // (onFulfilled, onRejected)
};

static std::vector<PromiseData*> all_promises;

// promise_then is used by promise_ctor below

static JSValue promise_constructor(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    if (args.size() < 2 || args[1].type != JSValue::Type::FUNCTION) {
        return JSValue::undefined();
    }
    auto* promise_gc = ctx->vm->heap()->alloc_object();
    auto* pd = new PromiseData();
    all_promises.push_back(pd);
    
    // Store reference to promise data on the object
    promise_gc->obj.set("[[PromiseState]]", JSValue::string("pending"));
    
    JSValue resolve_fn_val = JSValue::function(ctx->vm->create_native_fn(
        [](const std::vector<JSValue>& resolve_args, void* ctx_promise) -> JSValue {
            auto* pdata = static_cast<PromiseData*>(ctx_promise);
            if (pdata->state != PromiseData::PENDING) return JSValue::undefined();
            pdata->state = PromiseData::FULFILLED;
            pdata->result = resolve_args.size() > 1 ? resolve_args[1] : JSValue::undefined();
            for (auto& [onf, onr] : pdata->then_callbacks) {
                if (onf.type == JSValue::Type::FUNCTION) {
                    auto* fn = onf.function_val;
                    if (fn->native_fn) fn->native_fn({pdata->result}, fn->native_context);
                }
            }
            return JSValue::undefined();
        }, false, pd));
    
    JSValue reject_fn_val = JSValue::function(ctx->vm->create_native_fn(
        [](const std::vector<JSValue>& reject_args, void* ctx_promise) -> JSValue {
            auto* pdata = static_cast<PromiseData*>(ctx_promise);
            if (pdata->state != PromiseData::PENDING) return JSValue::undefined();
            pdata->state = PromiseData::REJECTED;
            pdata->result = reject_args.size() > 1 ? reject_args[1] : JSValue::undefined();
            for (auto& [onf, onr] : pdata->then_callbacks) {
                if (onr.type == JSValue::Type::FUNCTION) {
                    auto* fn = onr.function_val;
                    if (fn->native_fn) fn->native_fn({pdata->result}, fn->native_context);
                }
            }
            return JSValue::undefined();
        }, false, pd));
    
    // Call the executor with resolve, reject
    std::vector<JSValue> executor_args = {args[0], resolve_fn_val, reject_fn_val};
    auto* executor_fn = args[1].function_val;
    if (executor_fn->native_fn) {
        executor_fn->native_fn(executor_args, executor_fn->native_context);
    }
    
    return JSValue::object(&promise_gc->obj);
}

static JSValue promise_then_fn(const std::vector<JSValue>& args, void*) {
    if (args[0].type != JSValue::Type::OBJECT) return JSValue::undefined();
    // Simplified: find promise data and call callbacks
    JSValue on_fulfilled = args.size() > 1 ? args[1] : JSValue::undefined();
    JSValue on_rejected = args.size() > 2 ? args[2] : JSValue::undefined();
    if (on_fulfilled.type == JSValue::Type::FUNCTION) {
        auto* fn = on_fulfilled.function_val;
        if (fn->native_fn) fn->native_fn({}, fn->native_context);
    }
    return JSValue::undefined();
}

static JSValue promise_catch_fn(const std::vector<JSValue>& args, void*) {
    JSValue on_rejected = args.size() > 1 ? args[1] : JSValue::undefined();
    if (on_rejected.type == JSValue::Type::FUNCTION) {
        auto* fn = on_rejected.function_val;
        if (fn->native_fn) fn->native_fn({}, fn->native_context);
    }
    return JSValue::undefined();
}

static JSValue promise_resolve(const std::vector<JSValue>&, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    auto* promise_gc = ctx->vm->heap()->alloc_object();
    return JSValue::object(&promise_gc->obj);
}

static JSValue promise_reject(const std::vector<JSValue>&, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    auto* promise_gc = ctx->vm->heap()->alloc_object();
    return JSValue::object(&promise_gc->obj);
}

static JSValue promise_all(const std::vector<JSValue>&, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    auto* promise_gc = ctx->vm->heap()->alloc_object();
    return JSValue::object(&promise_gc->obj);
}

void register_promise_builtins(VM* vm) {
    auto* ctx = new PromiseCtx{vm};
    auto* promise_proto = vm->heap()->alloc_object();
    set_prototype_method(&promise_proto->obj, "then", make_fn(vm, promise_then_fn));
    set_prototype_method(&promise_proto->obj, "catch", make_fn(vm, promise_catch_fn));
    auto* promise_ctor = vm->heap()->alloc_object();
    promise_ctor->obj.set("prototype", JSValue::object(&promise_proto->obj));
    promise_ctor->obj.set("resolve", JSValue::function(make_fn(vm, promise_resolve, false, ctx)));
    promise_ctor->obj.set("reject", JSValue::function(make_fn(vm, promise_reject, false, ctx)));
    promise_ctor->obj.set("all", JSValue::function(make_fn(vm, promise_all, false, ctx)));
    promise_ctor->obj.set("race", JSValue::function(make_fn(vm, promise_all, false, ctx)));
    // Register Promise constructor
    auto native_fn_ptr = static_cast<JSFunction::NativeFn>(promise_constructor);
    auto* promise_fn = vm->create_native_fn(native_fn_ptr, true, ctx);
    vm->global_object()->set("Promise", JSValue::function(promise_fn));
}

} // namespace browser::js::builtins
