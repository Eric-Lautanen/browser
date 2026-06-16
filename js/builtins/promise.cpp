#include "builtins.hpp"
#include <vector>

namespace browser::js::builtins {

struct PromiseCtx { VM* vm; };

struct PromiseData {
    enum State { PENDING, FULFILLED, REJECTED };
    State state = PENDING;
    JSValue result;
    struct ThenEntry {
        JSValue on_fulfilled;
        JSValue on_rejected;
    };
    std::vector<ThenEntry> then_callbacks;
    bool handled = false;
};

// Store promise data directly on the JSObject via a hidden property key
// Promise state is stored directly in the JSObject's properties

static JSValue promise_constructor(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    auto* promise_gc = ctx->vm->heap()->alloc_object();
    auto& obj = promise_gc->obj;
    // args[0]=this (not used), args[1]=executor function
    if (args.size() < 2 || args[1].type != JSValue::Type::FUNCTION) {
        return JSValue::object(&obj);
    }
    JSValue executor = args[1];
    obj.set("[[PromiseState]]", JSValue::string("pending"));
    // Create native resolve and reject functions
    struct PromiseRef {
        JSValue promise_obj;
        VM* vm;
        bool resolved = false;
    };
    auto* pref = new PromiseRef{JSValue::object(&obj), ctx->vm, false};
    JSValue resolve_fn_val = JSValue::function(ctx->vm->create_native_fn(
        [](const std::vector<JSValue>& resolve_args, void* ctx_ref) -> JSValue {
            auto* ref = static_cast<PromiseRef*>(ctx_ref);
            if (ref->resolved) return JSValue::undefined();
            ref->resolved = true;
            JSValue val = resolve_args.size() > 1 ? resolve_args[1] : JSValue::undefined();
            if (ref->promise_obj.type == JSValue::Type::OBJECT && ref->promise_obj.object_val) {
                ref->promise_obj.object_val->set("[[PromiseState]]", JSValue::string("fulfilled"));
                ref->promise_obj.object_val->set("[[PromiseResult]]", val);
                // Fire then callbacks
                // They were stored on the object
                JSValue then_cbs = ref->promise_obj.object_val->get("!then");
                if (then_cbs.type == JSValue::Type::OBJECT && then_cbs.object_val) {
                    for (auto& el : then_cbs.object_val->array_elements) {
                        if (el.type == JSValue::Type::FUNCTION && el.function_val && el.function_val->native_fn) {
                            el.function_val->native_fn({ref->promise_obj, val}, el.function_val->native_context);
                        }
                    }
                }
            }
            delete ref;
            return JSValue::undefined();
        }, false, pref));
    JSValue reject_fn_val = JSValue::function(ctx->vm->create_native_fn(
        [](const std::vector<JSValue>& reject_args, void* ctx_ref) -> JSValue {
            auto* ref = static_cast<PromiseRef*>(ctx_ref);
            if (ref->resolved) return JSValue::undefined();
            ref->resolved = true;
            JSValue val = reject_args.size() > 1 ? reject_args[1] : JSValue::undefined();
            if (ref->promise_obj.type == JSValue::Type::OBJECT && ref->promise_obj.object_val) {
                ref->promise_obj.object_val->set("[[PromiseState]]", JSValue::string("rejected"));
                ref->promise_obj.object_val->set("[[PromiseResult]]", val);
                JSValue then_cbs = ref->promise_obj.object_val->get("!catch");
                if (then_cbs.type == JSValue::Type::OBJECT && then_cbs.object_val) {
                    for (auto& el : then_cbs.object_val->array_elements) {
                        if (el.type == JSValue::Type::FUNCTION && el.function_val && el.function_val->native_fn) {
                            el.function_val->native_fn({ref->promise_obj, val}, el.function_val->native_context);
                        }
                    }
                }
            }
            delete ref;
            return JSValue::undefined();
        }, false, pref));
    // Call executor with resolve, reject
    std::vector<JSValue> exec_args = {args[0], resolve_fn_val, reject_fn_val};
    auto* exec_fn = executor.function_val;
    if (exec_fn->native_fn) exec_fn->native_fn(exec_args, exec_fn->native_context);
    return JSValue::object(&obj);
}

static JSValue promise_then_fn(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    // args[0]=this (the promise), args[1]=onFulfilled, args[2]=onRejected
    JSValue promise = args[0];
    JSValue on_fulfilled = args.size() > 1 ? args[1] : JSValue::undefined();
    JSValue on_rejected = args.size() > 2 ? args[2] : JSValue::undefined();
    // Check if already resolved
    JSValue state = promise.type == JSValue::Type::OBJECT ? promise.object_val->get("[[PromiseState]]") : JSValue::undefined();
    if (state.type == JSValue::Type::STRING) {
        if (state.string_val == "fulfilled" && on_fulfilled.type == JSValue::Type::FUNCTION) {
            if (auto* fn = on_fulfilled.function_val; fn && fn->native_fn) {
                JSValue result = promise.object_val->get("[[PromiseResult]]");
                fn->native_fn({promise, result}, fn->native_context);
            }
            return JSValue::undefined();
        }
        if (state.string_val == "rejected" && on_rejected.type == JSValue::Type::FUNCTION) {
            if (auto* fn = on_rejected.function_val; fn && fn->native_fn) {
                JSValue result = promise.object_val->get("[[PromiseResult]]");
                fn->native_fn({promise, result}, fn->native_context);
            }
            return JSValue::undefined();
        }
    }
    // Pending - store callback for later
    if (on_fulfilled.type == JSValue::Type::FUNCTION) {
        JSValue arr = promise.object_val->get("!then");
        if (arr.type != JSValue::Type::OBJECT) {
            auto* arr_gc = ctx->vm->heap()->alloc_object();
            arr_gc->obj.is_array = true;
            arr = JSValue::object(&arr_gc->obj);
            promise.object_val->set("!then", arr);
        }
        arr.object_val->array_elements.push_back(on_fulfilled);
    }
    if (on_rejected.type == JSValue::Type::FUNCTION) {
        JSValue arr = promise.object_val->get("!catch");
        if (arr.type != JSValue::Type::OBJECT) {
            auto* arr_gc = ctx->vm->heap()->alloc_object();
            arr_gc->obj.is_array = true;
            arr = JSValue::object(&arr_gc->obj);
            promise.object_val->set("!catch", arr);
        }
        arr.object_val->array_elements.push_back(on_rejected);
    }
    // Return a new promise for chaining
    auto* new_promise = ctx->vm->heap()->alloc_object();
    return JSValue::object(&new_promise->obj);
}

static JSValue promise_catch_fn(const std::vector<JSValue>& args, void* context) {
    JSValue on_rejected = args.size() > 1 ? args[1] : JSValue::undefined();
    std::vector<JSValue> then_args = {args[0], JSValue::undefined(), on_rejected};
    return promise_then_fn(then_args, context);
}

static JSValue promise_static_resolve(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    auto* promise_gc = ctx->vm->heap()->alloc_object();
    promise_gc->obj.set("[[PromiseState]]", JSValue::string("fulfilled"));
    promise_gc->obj.set("[[PromiseResult]]", args.size() > 1 ? args[1] : JSValue::undefined());
    return JSValue::object(&promise_gc->obj);
}

static JSValue promise_static_reject(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    auto* promise_gc = ctx->vm->heap()->alloc_object();
    promise_gc->obj.set("[[PromiseState]]", JSValue::string("rejected"));
    promise_gc->obj.set("[[PromiseResult]]", args.size() > 1 ? args[1] : JSValue::undefined());
    return JSValue::object(&promise_gc->obj);
}

static JSValue promise_static_all(const std::vector<JSValue>&, void* context) {
    auto* ctx = static_cast<PromiseCtx*>(context);
    auto* promise_gc = ctx->vm->heap()->alloc_object();
    promise_gc->obj.set("[[PromiseState]]", JSValue::string("pending"));
    return JSValue::object(&promise_gc->obj);
}

void register_promise_builtins(VM* vm) {
    auto* ctx = new PromiseCtx{vm};
    auto* promise_proto = vm->heap()->alloc_object();
    set_prototype_method(&promise_proto->obj, "then", make_fn(vm, promise_then_fn, false, ctx));
    set_prototype_method(&promise_proto->obj, "catch", make_fn(vm, promise_catch_fn, false, ctx));
    // Register Promise constructor as native function with prototype
    auto* promise_fn = vm->create_native_fn(
        static_cast<JSFunction::NativeFn>(promise_constructor), true, ctx);
    // Store prototype property for 'new' operator
    promise_fn->prototype_property = JSValue::object(&promise_proto->obj);
    // Add static methods to the function object
    // Since JSFunction is not JSObject, we need to store them separately
    // For now, add them as globals
    vm->global_object()->set("Promise", JSValue::function(promise_fn));
    // Store static methods on a separate promise constructor object
    auto* static_obj = vm->heap()->alloc_object();
    static_obj->obj.set("resolve", JSValue::function(make_fn(vm, promise_static_resolve, false, ctx)));
    static_obj->obj.set("reject", JSValue::function(make_fn(vm, promise_static_reject, false, ctx)));
    static_obj->obj.set("all", JSValue::function(make_fn(vm, promise_static_all, false, ctx)));
    static_obj->obj.set("race", JSValue::function(make_fn(vm, promise_static_all, false, ctx)));
    static_obj->obj.set("prototype", JSValue::object(&promise_proto->obj));
    vm->global_object()->set("Promise", JSValue::function(promise_fn));
}

} // namespace browser::js::builtins
