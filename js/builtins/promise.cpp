#include "builtins.hpp"

#include <string>
#include <vector>

namespace browser::js::builtins {

    // Promise state lives directly on the JSObject under hidden keys so that
    // engine-constructed promises (fetch, timers) share the same representation.
    static constexpr const char *kState = "[[PromiseState]]";
    static constexpr const char *kResult = "[[PromiseResult]]";
    // Reaction list: flat triples [handler, chained, is_fulfill_side, ...].
    // `chained` may be undefined when the handler manages its own settlement
    // (used internally by Promise.all / race / any).
    static constexpr const char *kReactions = "!reactions";

    struct PromiseCtx {
        VM *vm;
    };

    struct FwdCtx {
        VM *vm;
        JSObject *target;
        bool ok;
    };

    static bool promise_like(const JSValue &v) {
        if (v.type != JSValue::Type::OBJECT || !v.object_val)
            return false;
        JSValue s = v.object_val->get(kState);
        return s.type == JSValue::Type::STRING;
    }

    static JSValue get_promise_field(const JSValue &p, const char *key) {
        if (!promise_like(p))
            return JSValue::undefined();
        return p.object_val->get(key);
    }

    // Creates a pending promise object wired to the shared prototype so that
    // `.then()`/`.catch()` resolve through the normal property lookup chain.
    static JSValue make_promise(VM *vm) {
        auto *obj_gc = vm->heap()->alloc_object();
        auto &obj = obj_gc->obj;
        JSValue ctor_val = vm->global_object()->get("Promise");
        if (ctor_val.type == JSValue::Type::FUNCTION && ctor_val.function_val &&
            ctor_val.function_val->prototype_property.type == JSValue::Type::OBJECT) {
            obj.prototype = ctor_val.function_val->prototype_property;
        }
        obj.set(kState, JSValue::string("pending"));
        return JSValue::object(&obj);
    }

    // Engine-facing helper: any builtin that hands a promise to script code must
    // use this so prototype methods are reachable.
    JSValue make_promise_object(VM *vm) {
        return make_promise(vm);
    }

    static void settle(VM *vm, JSObject *p, bool fulfilled, JSValue val);

    // Adopts a still-pending inner promise: forwards its eventual settlement to
    // the target promise.
    static void adopt_pending(VM *vm, JSObject *target, const JSValue &inner) {
        auto *ctx_ok = new FwdCtx{vm, target, true};
        auto *ctx_err = new FwdCtx{vm, target, false};
        JSValue h_ok = JSValue::function(vm->create_native_fn(
            [](const std::vector<JSValue> &args, void *c) -> JSValue {
                auto *cx = static_cast<FwdCtx *>(c);
                JSValue v = args.size() > 1 ? args[1] : JSValue::undefined();
                settle(cx->vm, cx->target, true, v);
                return JSValue::undefined();
            },
            false,
            ctx_ok));
        JSValue h_err = JSValue::function(vm->create_native_fn(
            [](const std::vector<JSValue> &args, void *c) -> JSValue {
                auto *cx = static_cast<FwdCtx *>(c);
                JSValue v = args.size() > 1 ? args[1] : JSValue::undefined();
                settle(cx->vm, cx->target, false, v);
                return JSValue::undefined();
            },
            false,
            ctx_err));

        JSValue arr = inner.object_val->get(kReactions);
        if (arr.type != JSValue::Type::OBJECT || !arr.object_val) {
            auto *arr_gc = vm->heap()->alloc_object();
            arr_gc->obj.is_array = true;
            arr = JSValue::object(&arr_gc->obj);
            inner.object_val->set(kReactions, arr);
        }
        auto &els = arr.object_val->array_elements;
        els.push_back(h_ok);
        els.push_back(JSValue::undefined());  // no separate chain: handler settles target
        els.push_back(JSValue::boolean(true));
        els.push_back(h_err);
        els.push_back(JSValue::undefined());
        els.push_back(JSValue::boolean(false));
    }

    static void resolve_value(VM *vm, JSObject *p, JSValue val) {
        if (promise_like(val)) {
            JSValue inner_state = val.object_val->get(kState);
            if (inner_state.type == JSValue::Type::STRING && inner_state.string_val != "pending") {
                settle(vm, p, inner_state.string_val == "fulfilled", val.object_val->get(kResult));
            } else {
                adopt_pending(vm, p, val);
            }
            return;
        }
        settle(vm, p, true, val);
    }

    static void add_reaction(VM *vm, JSObject *p, JSValue handler, JSValue chained, bool fulfill_side) {
        JSValue arr = p->get(kReactions);
        if (arr.type != JSValue::Type::OBJECT || !arr.object_val) {
            auto *arr_gc = vm->heap()->alloc_object();
            arr_gc->obj.is_array = true;
            arr = JSValue::object(&arr_gc->obj);
            p->set(kReactions, arr);
        }
        auto &els = arr.object_val->array_elements;
        els.push_back(handler);
        els.push_back(chained);
        els.push_back(JSValue::boolean(fulfill_side));
    }

    static void drain_reactions(VM *vm, JSObject *p, bool fulfilled) {
        JSValue reactions = p->get(kReactions);
        // Reset before running; settle() ignores further additions to a settled p.
        if (auto *fresh = vm->heap()->alloc_object()) {
            fresh->obj.is_array = true;
            p->set(kReactions, JSValue::object(&fresh->obj));
        }
        if (reactions.type != JSValue::Type::OBJECT || !reactions.object_val)
            return;

        JSValue result = p->get(kResult);
        auto &els = reactions.object_val->array_elements;
        for (size_t i = 0; i + 2 < els.size(); i += 3) {
            bool fulfill_side = els[i + 2].bool_val;
            if (fulfill_side != fulfilled)
                continue;
            JSValue handler = els[i];
            JSValue chained = els[i + 1];

            if (!fulfilled && !(handler.type == JSValue::Type::FUNCTION)) {
                // Unhandled rejection: forward to the chained promise.
                if (chained.type == JSValue::Type::OBJECT && chained.object_val)
                    settle(vm, chained.object_val, false, result);
                continue;
            }

            JSValue out = result;
            if (handler.type == JSValue::Type::FUNCTION && handler.function_val)
                out = vm->invoke(handler, {result});

            if (!(chained.type == JSValue::Type::OBJECT) || !chained.object_val)
                continue;
            resolve_value(vm, chained.object_val, out);
        }
    }

    static void settle(VM *vm, JSObject *p, bool fulfilled, JSValue val) {
        JSValue cur = p->get(kState);
        if (cur.type == JSValue::Type::STRING && cur.string_val != "pending")
            return;
        p->set(kState, JSValue::string(fulfilled ? "fulfilled" : "rejected"));
        p->set(kResult, val);
        drain_reactions(vm, p, fulfilled);
    }

    // ---------------------------------------------------------------------------
    // Constructor + prototype methods
    // ---------------------------------------------------------------------------

    static JSValue promise_constructor(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<PromiseCtx *>(context);
        VM *vm = ctx->vm;
        JSValue p = make_promise(vm);

        struct ResolveRef {
            VM *vm;
            JSObject *promise;
        };
        auto *rref = new ResolveRef{vm, p.object_val};
        auto *eref = new ResolveRef{vm, p.object_val};

        JSValue resolve_fn = JSValue::function(vm->create_native_fn(
            [](const std::vector<JSValue> &a, void *c) -> JSValue {
                auto *ref = static_cast<ResolveRef *>(c);
                JSValue v = a.size() > 1 ? a[1] : JSValue::undefined();
                resolve_value(ref->vm, ref->promise, v);
                return JSValue::undefined();
            },
            false,
            rref));
        JSValue reject_fn = JSValue::function(vm->create_native_fn(
            [](const std::vector<JSValue> &a, void *c) -> JSValue {
                auto *ref = static_cast<ResolveRef *>(c);
                JSValue v = a.size() > 1 ? a[1] : JSValue::undefined();
                settle(ref->vm, ref->promise, false, v);
                return JSValue::undefined();
            },
            false,
            eref));

        if (args.size() >= 2 && args[1].type == JSValue::Type::FUNCTION && args[1].function_val) {
            vm->invoke(args[1], {resolve_fn, reject_fn});
        } else {
            // Spec: executor must be callable.
            settle(vm, p.object_val, false, JSValue::string("TypeError: Promise resolver is not a function"));
        }
        return p;
    }

    static JSValue promise_then_impl(VM *vm, const JSValue &p, JSValue on_fulfilled, JSValue on_rejected) {
        JSValue p2 = make_promise(vm);

        JSValue state = get_promise_field(p, kState);
        bool settled = state.type == JSValue::Type::STRING && state.string_val != "pending";

        if (settled) {
            bool fulfilled = state.string_val == "fulfilled";
            JSValue result = p.object_val->get(kResult);
            if (fulfilled || on_rejected.type == JSValue::Type::FUNCTION) {
                JSValue out = result;
                if ((fulfilled ? on_fulfilled : on_rejected).type == JSValue::Type::FUNCTION)
                    out = vm->invoke(fulfilled ? on_fulfilled : on_rejected, {result});
                resolve_value(vm, p2.object_val, out);
            } else {
                settle(vm, p2.object_val, false, result);
            }
            return p2;
        }

        add_reaction(vm, p.object_val, on_fulfilled, p2, true);
        add_reaction(vm, p.object_val, on_rejected, p2, false);
        return p2;
    }

    static JSValue promise_then_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<PromiseCtx *>(context);
        JSValue p = !args.empty() ? args[0] : JSValue::undefined();
        JSValue on_fulfilled = args.size() > 1 ? args[1] : JSValue::undefined();
        JSValue on_rejected = args.size() > 2 ? args[2] : JSValue::undefined();
        return promise_then_impl(ctx->vm, p, on_fulfilled, on_rejected);
    }

    static JSValue promise_catch_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<PromiseCtx *>(context);
        JSValue p = !args.empty() ? args[0] : JSValue::undefined();
        JSValue on_rejected = args.size() > 1 ? args[1] : JSValue::undefined();
        return promise_then_impl(ctx->vm, p, JSValue::undefined(), on_rejected);
    }

    static JSValue promise_finally_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<PromiseCtx *>(context);
        VM *vm = ctx->vm;
        JSValue p = !args.empty() ? args[0] : JSValue::undefined();
        JSValue cb = args.size() > 1 ? args[1] : JSValue::undefined();

        struct CbPass {
            VM *vm;
            JSValue cb;
        };

        // Fulfillment: run cb (ignoring its result), pass the value through.
        auto *okp = new CbPass{vm, cb};
        JSValue on_ok = JSValue::function(vm->create_native_fn(
            [](const std::vector<JSValue> &a, void *c) -> JSValue {
                auto *cx = static_cast<CbPass *>(c);
                if (cx->cb.type == JSValue::Type::FUNCTION && cx->cb.function_val)
                    cx->vm->invoke(cx->cb, {});
                return a.size() > 1 ? a[1] : JSValue::undefined();
            },
            false,
            okp));

        // Rejection: run cb, then return an already-rejected promise carrying the
        // original reason — resolve_value adopts it and keeps p2 rejected.
        auto *rp = new CbPass{vm, cb};
        JSValue on_err = JSValue::function(vm->create_native_fn(
            [](const std::vector<JSValue> &a, void *c) -> JSValue {
                auto *cx = static_cast<CbPass *>(c);
                if (cx->cb.type == JSValue::Type::FUNCTION && cx->cb.function_val)
                    cx->vm->invoke(cx->cb, {});
                JSValue rejected = make_promise(cx->vm);
                rejected.object_val->set(kState, JSValue::string("rejected"));
                rejected.object_val->set(kResult, a.size() > 1 ? a[1] : JSValue::undefined());
                return rejected;
            },
            false,
            rp));

        return promise_then_impl(vm, p, on_ok, on_err);
    }

    // ---------------------------------------------------------------------------
    // Statics
    // ---------------------------------------------------------------------------

    struct AllCtx {
        VM *vm;
        JSObject *aggregate;
        JSObject *results;
        u32 remaining;
        bool rejected = false;
    };

    static void all_check_done(AllCtx *ac) {
        if (ac->rejected || ac->remaining > 0)
            return;
        JSValue arr = JSValue::object(ac->results);
        settle(ac->vm, ac->aggregate, true, arr);
    }

    static JSValue promise_static_resolve(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<PromiseCtx *>(context);
        JSValue p = make_promise(ctx->vm);
        JSValue v = args.size() > 1 ? args[1] : JSValue::undefined();
        resolve_value(ctx->vm, p.object_val, v);
        return p;
    }

    static JSValue promise_static_reject(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<PromiseCtx *>(context);
        JSValue p = make_promise(ctx->vm);
        JSValue v = args.size() > 1 ? args[1] : JSValue::undefined();
        settle(ctx->vm, p.object_val, false, v);
        return p;
    }

    static JSValue promise_static_all(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<PromiseCtx *>(context);
        VM *vm = ctx->vm;
        JSValue agg = make_promise(vm);

        JSValue input = args.size() > 1 ? args[1] : JSValue::undefined();
        if (!is_array(input)) {
            settle(vm, agg.object_val, false, JSValue::string("TypeError: Promise.all expects an array"));
            return agg;
        }
        auto &els = input.object_val->array_elements;

        auto *results_obj = vm->heap()->alloc_object();
        results_obj->obj.is_array = true;
        results_obj->obj.array_elements.assign(els.size(), JSValue::undefined());

        auto *ac = new AllCtx{vm, agg.object_val, &results_obj->obj, static_cast<u32>(els.size()), false};

        for (u32 i = 0; i < els.size(); i++) {
            JSValue el = els[i];
            if (!promise_like(el)) {
                results_obj->obj.array_elements[i] = el;
                ac->remaining--;
                all_check_done(ac);
                continue;
            }
            struct IdxCtx {
                AllCtx *ac;
                u32 index;
            };
            auto *ic = new IdxCtx{ac, i};
            JSValue on_ok = JSValue::function(vm->create_native_fn(
                [](const std::vector<JSValue> &a, void *c) -> JSValue {
                    auto *ix = static_cast<IdxCtx *>(c);
                    JSValue v = a.size() > 1 ? a[1] : JSValue::undefined();
                    ix->ac->results->array_elements[ix->index] = v;
                    if (ix->ac->remaining > 0)
                        ix->ac->remaining--;
                    all_check_done(ix->ac);
                    return JSValue::undefined();
                },
                false,
                ic));
            JSValue on_err = JSValue::function(vm->create_native_fn(
                [](const std::vector<JSValue> &a, void *c) -> JSValue {
                    auto *ix = static_cast<IdxCtx *>(c);
                    ix->ac->rejected = true;
                    JSValue v = a.size() > 1 ? a[1] : JSValue::undefined();
                    settle(ix->ac->vm, ix->ac->aggregate, false, v);
                    return JSValue::undefined();
                },
                false,
                ic));
            add_reaction(vm, el.object_val, on_ok, JSValue::undefined(), true);
            add_reaction(vm, el.object_val, on_err, JSValue::undefined(), false);
            if (promise_like(el)) {
                JSValue st = el.object_val->get(kState);
                // Already-settled elements must count immediately.
                if (st.type == JSValue::Type::STRING && st.string_val != "pending") {
                    // drain_reactions already fired during settle; emulate by direct call.
                    if (st.string_val == "fulfilled") {
                        // Handler was queued after settlement — invoke manually.
                        vm->invoke(on_ok, {el.object_val->get(kResult)});
                    } else {
                        vm->invoke(on_err, {el.object_val->get(kResult)});
                    }
                }
            }
        }
        all_check_done(ac);
        return agg;
    }

    static JSValue promise_static_race(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<PromiseCtx *>(context);
        VM *vm = ctx->vm;
        JSValue agg = make_promise(vm);

        JSValue input = args.size() > 1 ? args[1] : JSValue::undefined();
        if (!is_array(input)) {
            settle(vm, agg.object_val, false, JSValue::string("TypeError: Promise.race expects an array"));
            return agg;
        }
        for (JSValue el : input.object_val->array_elements) {
            if (!promise_like(el))
                continue;
            JSValue st = el.object_val->get(kState);
            bool settled = st.type == JSValue::Type::STRING && st.string_val != "pending";
            auto *ok_ctx = new FwdCtx{vm, agg.object_val, true};
            auto *err_ctx = new FwdCtx{vm, agg.object_val, false};
            JSValue on_ok = JSValue::function(vm->create_native_fn(
                [](const std::vector<JSValue> &a, void *c) -> JSValue {
                    auto *cx = static_cast<FwdCtx *>(c);
                    settle(cx->vm, cx->target, true, a.size() > 1 ? a[1] : JSValue::undefined());
                    return JSValue::undefined();
                },
                false,
                ok_ctx));
            JSValue on_err = JSValue::function(vm->create_native_fn(
                [](const std::vector<JSValue> &a, void *c) -> JSValue {
                    auto *cx = static_cast<FwdCtx *>(c);
                    settle(cx->vm, cx->target, false, a.size() > 1 ? a[1] : JSValue::undefined());
                    return JSValue::undefined();
                },
                false,
                err_ctx));
            add_reaction(vm, el.object_val, on_ok, JSValue::undefined(), true);
            add_reaction(vm, el.object_val, on_err, JSValue::undefined(), false);
            if (settled) {
                bool ok = st.string_val == "fulfilled";
                vm->invoke(ok ? on_ok : on_err, {el.object_val->get(kResult)});
            }
        }
        return agg;
    }

    void register_promise_builtins(VM *vm) {
        auto *ctx = new PromiseCtx{vm};
        auto *promise_proto = vm->heap()->alloc_object();
        set_prototype_method(&promise_proto->obj, "then", make_fn(vm, promise_then_fn, false, ctx));
        set_prototype_method(&promise_proto->obj, "catch", make_fn(vm, promise_catch_fn, false, ctx));
        set_prototype_method(&promise_proto->obj, "finally", make_fn(vm, promise_finally_fn, false, ctx));

        auto *promise_fn = vm->create_native_fn(promise_constructor, true, ctx);
        promise_fn->name = "Promise";
        promise_fn->prototype_property = JSValue::object(&promise_proto->obj);
        promise_fn->properties["prototype"] = JSValue::object(&promise_proto->obj);
        promise_fn->properties["resolve"] = JSValue::function(make_fn(vm, promise_static_resolve, false, ctx));
        promise_fn->properties["reject"] = JSValue::function(make_fn(vm, promise_static_reject, false, ctx));
        promise_fn->properties["all"] = JSValue::function(make_fn(vm, promise_static_all, false, ctx));
        promise_fn->properties["race"] = JSValue::function(make_fn(vm, promise_static_race, false, ctx));
        vm->global_object()->set("Promise", JSValue::function(promise_fn));
    }

}  // namespace browser::js::builtins
