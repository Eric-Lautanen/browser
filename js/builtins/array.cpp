#include "builtins.hpp"

#include <cmath>

namespace browser::js::builtins {

struct ArrCtx { VM* vm; };

static std::vector<JSValue>& elems(const JSValue& val) {
    return val.object_val->array_elements;
}

static JSValue arr_push(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    for (u32 i = 1; i < args.size(); i++) el.push_back(args[i]);
    return JSValue::number(static_cast<f64>(el.size()));
}

static JSValue arr_pop(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    if (el.empty()) return JSValue::undefined();
    auto val = el.back();
    el.pop_back();
    return val;
}

static JSValue arr_shift(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    if (el.empty()) return JSValue::undefined();
    auto val = el.front();
    el.erase(el.begin());
    return val;
}

static JSValue arr_unshift(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    el.insert(el.begin(), args.begin() + 1, args.end());
    return JSValue::number(static_cast<f64>(el.size()));
}

static JSValue arr_for_each(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<ArrCtx *>(context);
    // J-M6: snapshot elements; callbacks may mutate the receiver array and
    // reallocate its storage out from under the loop.
    const std::vector<JSValue> el = elems(args[0]);
    if (args.size() < 2) return JSValue::undefined();
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION) return JSValue::undefined();
    // J-C1: bytecode callbacks are invoked through VM::invoke.
    for (u32 i = 0; i < el.size(); i++) {
        ctx->vm->invoke(callback, {el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
    }
    return JSValue::undefined();
}

static JSValue arr_map(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    if (args.size() < 2)
        return result_val;
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION)
        return result_val;
    for (u32 i = 0; i < el.size(); i++) {
        result.push_back(ctx->vm->invoke(callback, {el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]));
    }
    return result_val;
}

static JSValue arr_filter(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    if (args.size() < 2)
        return result_val;
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION)
        return result_val;
    for (u32 i = 0; i < el.size(); i++) {
        JSValue keep = ctx->vm->invoke(callback, {el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
        if (keep.is_truthy()) result.push_back(el[i]);
    }
    return result_val;
}

static JSValue arr_reduce(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<ArrCtx *>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    if (args.size() < 2) return JSValue::undefined();
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION) return JSValue::undefined();
    u32 start = 0;
    JSValue acc;
    bool has_initial = args.size() > 2;
    if (has_initial) {
        acc = args[2];
        start = 0;
    } else if (!el.empty()) {
        acc = el[0];
        start = 1;
    } else {
        return JSValue::undefined();
    }
    for (u32 i = start; i < el.size(); i++) {
        acc = ctx->vm->invoke(callback, {acc, el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
    }
    return acc;
}

static JSValue arr_reduce_right(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<ArrCtx *>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    if (args.size() < 2) return JSValue::undefined();
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION) return JSValue::undefined();
    bool has_initial = args.size() > 2;
    i32 start;
    JSValue acc;
    if (has_initial) {
        acc = args[2];
        start = static_cast<i32>(el.size()) - 1;
    } else if (!el.empty()) {
        acc = el.back();
        start = static_cast<i32>(el.size()) - 2;
    } else {
        return JSValue::undefined();
    }
    for (i32 i = start; i >= 0; i--) {
        acc = ctx->vm->invoke(callback, {acc, el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
    }
    return acc;
}

static JSValue arr_find(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<ArrCtx *>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    if (args.size() < 2) return JSValue::undefined();
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION) return JSValue::undefined();
    for (u32 i = 0; i < el.size(); i++) {
        JSValue found = ctx->vm->invoke(callback, {el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
        if (found.is_truthy()) return el[i];
    }
    return JSValue::undefined();
}

static JSValue arr_find_index(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<ArrCtx *>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    if (args.size() < 2) return JSValue::number(-1);
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION) return JSValue::number(-1);
    for (u32 i = 0; i < el.size(); i++) {
        JSValue found = ctx->vm->invoke(callback, {el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
        if (found.is_truthy()) return JSValue::number(static_cast<f64>(i));
    }
    return JSValue::number(-1);
}

static JSValue arr_some(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<ArrCtx *>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    if (args.size() < 2) return JSValue::boolean(false);
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION) return JSValue::boolean(false);
    for (u32 i = 0; i < el.size(); i++) {
        JSValue found = ctx->vm->invoke(callback, {el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
        if (found.is_truthy()) return JSValue::boolean(true);
    }
    return JSValue::boolean(false);
}

static JSValue arr_every(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<ArrCtx *>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    if (args.size() < 2) return JSValue::boolean(true);
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION) return JSValue::boolean(true);
    for (u32 i = 0; i < el.size(); i++) {
        JSValue ok = ctx->vm->invoke(callback, {el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
        if (!ok.is_truthy())
            return JSValue::boolean(false);
    }
    return JSValue::boolean(true);
}

static JSValue arr_includes(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    if (args.size() < 2) return JSValue::boolean(false);
    for (auto& v : el) {
        if (v.type == args[1].type) {
            if (v.type == JSValue::Type::NUMBER) {
                if (v.number_val == args[1].number_val) return JSValue::boolean(true);
            } else if (v.type == JSValue::Type::STRING) {
                if (v.string_val == args[1].string_val) return JSValue::boolean(true);
            } else if (v.type == JSValue::Type::BOOLEAN) {
                if (v.bool_val == args[1].bool_val) return JSValue::boolean(true);
            } else if (v.object_val == args[1].object_val) {
                return JSValue::boolean(true);
            }
        }
    }
    return JSValue::boolean(false);
}

static JSValue arr_index_of(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    if (args.size() < 2) return JSValue::number(-1);
    i32 from = get_int_arg(args, 2, 0);
    if (from < 0) from = 0;
    for (u32 i = static_cast<u32>(from); i < el.size(); i++) {
        if (el[i].type == args[1].type) {
            if (el[i].type == JSValue::Type::NUMBER) {
                if (el[i].number_val == args[1].number_val) return JSValue::number(static_cast<f64>(i));
            } else if (el[i].type == JSValue::Type::STRING) {
                if (el[i].string_val == args[1].string_val) return JSValue::number(static_cast<f64>(i));
            } else if (el[i].type == JSValue::Type::BOOLEAN) {
                if (el[i].bool_val == args[1].bool_val) return JSValue::number(static_cast<f64>(i));
            }
        }
    }
    return JSValue::number(-1);
}

static JSValue arr_last_index_of(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    if (args.size() < 2 || el.empty()) return JSValue::number(-1);
    i32 from = get_int_arg(args, 2, static_cast<i32>(el.size()) - 1);
    if (from < 0) from = 0;
    if (from >= static_cast<i32>(el.size())) from = static_cast<i32>(el.size()) - 1;
    for (i32 i = from; i >= 0; i--) {
        u32 u = static_cast<u32>(i);
        if (el[u].type == args[1].type) {
            if (el[u].type == JSValue::Type::NUMBER) {
                if (el[u].number_val == args[1].number_val) return JSValue::number(static_cast<f64>(i));
            } else if (el[u].type == JSValue::Type::STRING) {
                if (el[u].string_val == args[1].string_val) return JSValue::number(static_cast<f64>(i));
            } else if (el[u].type == JSValue::Type::BOOLEAN) {
                if (el[u].bool_val == args[1].bool_val) return JSValue::number(static_cast<f64>(i));
            }
        }
    }
    return JSValue::number(-1);
}

static JSValue arr_splice_fn(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    auto& el = elems(args[0]);
    i32 start = get_int_arg(args, 1, 0);
    u32 dc = args.size() > 2 ? static_cast<u32>(std::max(0, get_int_arg(args, 2))) : static_cast<u32>(el.size());
    std::vector<JSValue> items;
    for (u32 i = 3; i < args.size(); i++) items.push_back(args[i]);
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    if (start < 0) start = std::max(0, static_cast<i32>(el.size()) + start);
    if (start > static_cast<i32>(el.size())) start = static_cast<i32>(el.size());
    u32 ustart = static_cast<u32>(start);
    u32 dc_clamped = std::min(dc, static_cast<u32>(el.size() - ustart));
    for (u32 i = 0; i < dc_clamped; i++) result.push_back(el[ustart + i]);
    auto it = el.begin() + ustart;
    el.erase(it, it + dc_clamped);
    el.insert(el.begin() + ustart, items.begin(), items.end());
    return result_val;
}

static JSValue arr_slice(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    auto& el = elems(args[0]);
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    i32 start = get_int_arg(args, 1, 0);
    i32 end_val = args.size() > 2 ? get_int_arg(args, 2) : static_cast<i32>(el.size());
    i32 len = static_cast<i32>(el.size());
    if (start < 0) start = std::max(0, len + start);
    if (end_val < 0) end_val = std::max(0, len + end_val);
    if (end_val > len) end_val = len;
    for (i32 i = start; i < end_val && i < len; i++) result.push_back(el[i]);
    return result_val;
}

static JSValue arr_join(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    std::string sep = args.size() > 1 ? args[1].to_string() : ",";
    std::string result;
    for (u32 i = 0; i < el.size(); i++) {
        if (i > 0) result += sep;
        result += el[i].to_string();
    }
    return JSValue::string(result);
}

static JSValue arr_concat(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    auto& el = elems(args[0]);
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    result.insert(result.end(), el.begin(), el.end());
    for (u32 i = 1; i < args.size(); i++) {
        if (is_array(args[i])) {
            auto& other = elems(args[i]);
            result.insert(result.end(), other.begin(), other.end());
        } else {
            result.push_back(args[i]);
        }
    }
    return result_val;
}

static JSValue arr_reverse(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    std::reverse(el.begin(), el.end());
    return args[0];
}

static JSValue arr_sort(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<ArrCtx *>(context);
    auto& el = elems(args[0]);
    JSValue cmp_fn = args.size() > 1 ? args[1] : JSValue::undefined();
    if (cmp_fn.type == JSValue::Type::FUNCTION) {
        // J-C9: sanitize comparator results to avoid inconsistent strict-weak
        // ordering (NaN returns previously caused UB in std::sort).
        std::sort(el.begin(), el.end(), [&](const JSValue &a, const JSValue &b) {
            JSValue r = ctx->vm->invoke(cmp_fn, {a, b}, args[0]);
            f64 n = r.to_number();
            if (std::isnan(n))
                return false;
            return n < 0;
        });
    } else {
        std::sort(el.begin(), el.end(), [](const JSValue& a, const JSValue& b) {
            return a.to_string() < b.to_string();
        });
    }
    return args[0];
}

static JSValue arr_fill(const std::vector<JSValue>& args, void*) {
    auto& el = elems(args[0]);
    JSValue val = args.size() > 1 ? args[1] : JSValue::undefined();
    i32 start = get_int_arg(args, 2, 0);
    i32 end_val = args.size() > 3 ? get_int_arg(args, 3) : static_cast<i32>(el.size());
    i32 len = static_cast<i32>(el.size());
    if (start < 0) start = std::max(0, len + start);
    if (end_val < 0) end_val = std::max(0, len + end_val);
    for (i32 i = start; i < end_val && i < len; i++) el[i] = val;
    return args[0];
}

static JSValue arr_flat(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    auto& el = elems(args[0]);
    i32 depth = get_int_arg(args, 1, 1);
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    std::function<void(const std::vector<JSValue>&, i32)> flatten = [&](const std::vector<JSValue>& src, i32 d) {
        for (auto& v : src) {
            if (d > 0 && is_array(v)) {
                flatten(get_array_elements(v), d - 1);
            } else {
                result.push_back(v);
            }
        }
    };
    flatten(el, depth);
    return result_val;
}

static JSValue arr_flat_map(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    const std::vector<JSValue> el = elems(args[0]);  // J-M6: snapshot
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    if (args.size() < 2)
        return result_val;
    JSValue callback = args[1];
    if (callback.type != JSValue::Type::FUNCTION)
        return result_val;
    for (u32 i = 0; i < el.size(); i++) {
        JSValue mapped = ctx->vm->invoke(callback, {el[i], JSValue::number(static_cast<f64>(i)), args[0]}, args[0]);
        if (is_array(mapped)) {
            auto& mapped_el = get_array_elements(mapped);
            result.insert(result.end(), mapped_el.begin(), mapped_el.end());
        } else {
            result.push_back(mapped);
        }
    }
    return result_val;
}

static JSValue arr_is_array(const std::vector<JSValue>& args, void*) {
    return JSValue::boolean(args.size() > 1 ? is_array(args[1]) : false);
}

static JSValue arr_from(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    if (args.size() > 1 && is_array(args[1])) {
        auto& src = get_array_elements(args[1]);
        if (args.size() > 2 && args[2].type == JSValue::Type::FUNCTION) {
            JSValue map_fn = args[2];
            for (u32 i = 0; i < src.size(); i++) {
                result.push_back(ctx->vm->invoke(map_fn, {src[i], JSValue::number(static_cast<f64>(i))}, args[0]));
            }
            return result_val;
        }
        result = src;
    }
    return result_val;
}

static JSValue arr_of(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ArrCtx*>(context);
    JSValue result_val = make_array_value(ctx->vm);
    auto &result = result_val.object_val->array_elements;
    for (u32 i = 1; i < args.size(); i++) result.push_back(args[i]);
    return result_val;
}

void register_array_prototype(VM* vm) {
    auto* arr_proto = vm->heap()->alloc_object();
    arr_proto->obj.is_array = true;
    auto* ctx = new ArrCtx{vm};
    set_prototype_method(&arr_proto->obj, "push", make_fn(vm, arr_push));
    set_prototype_method(&arr_proto->obj, "pop", make_fn(vm, arr_pop));
    set_prototype_method(&arr_proto->obj, "shift", make_fn(vm, arr_shift));
    set_prototype_method(&arr_proto->obj, "unshift", make_fn(vm, arr_unshift));
    set_prototype_method(&arr_proto->obj, "forEach", make_fn(vm, arr_for_each, false, ctx));
    set_prototype_method(&arr_proto->obj, "map", make_fn(vm, arr_map, false, ctx));
    set_prototype_method(&arr_proto->obj, "filter", make_fn(vm, arr_filter, false, ctx));
    set_prototype_method(&arr_proto->obj, "reduce", make_fn(vm, arr_reduce, false, ctx));
    set_prototype_method(&arr_proto->obj, "reduceRight", make_fn(vm, arr_reduce_right, false, ctx));
    set_prototype_method(&arr_proto->obj, "find", make_fn(vm, arr_find, false, ctx));
    set_prototype_method(&arr_proto->obj, "findIndex", make_fn(vm, arr_find_index, false, ctx));
    set_prototype_method(&arr_proto->obj, "some", make_fn(vm, arr_some, false, ctx));
    set_prototype_method(&arr_proto->obj, "every", make_fn(vm, arr_every, false, ctx));
    set_prototype_method(&arr_proto->obj, "includes", make_fn(vm, arr_includes));
    set_prototype_method(&arr_proto->obj, "indexOf", make_fn(vm, arr_index_of));
    set_prototype_method(&arr_proto->obj, "lastIndexOf", make_fn(vm, arr_last_index_of));
    set_prototype_method(&arr_proto->obj, "splice", make_fn(vm, arr_splice_fn, false, ctx));
    set_prototype_method(&arr_proto->obj, "slice", make_fn(vm, arr_slice, false, ctx));
    set_prototype_method(&arr_proto->obj, "join", make_fn(vm, arr_join));
    set_prototype_method(&arr_proto->obj, "concat", make_fn(vm, arr_concat, false, ctx));
    set_prototype_method(&arr_proto->obj, "reverse", make_fn(vm, arr_reverse));
    set_prototype_method(&arr_proto->obj, "sort", make_fn(vm, arr_sort, false, ctx));
    set_prototype_method(&arr_proto->obj, "fill", make_fn(vm, arr_fill));
    set_prototype_method(&arr_proto->obj, "flat", make_fn(vm, arr_flat, false, ctx));
    set_prototype_method(&arr_proto->obj, "flatMap", make_fn(vm, arr_flat_map, false, ctx));
    arr_proto->obj.set("length", JSValue::number(0));
    // The Array global is a native function (registered in vm.cpp).
    // Set its prototype_property so 'new Array()' creates objects with the right prototype.
    JSValue existing_arr = vm->global_object()->get("Array");
    if (existing_arr.type == JSValue::Type::FUNCTION) {
        existing_arr.function_val->prototype_property = JSValue::object(&arr_proto->obj);
    }
    // Store static methods as separate globals for now (JSFunction can't hold properties).
    // In a full implementation, JSFunction would also be a JSObject.
    vm->global_object()->set("Array_isArray", JSValue::function(make_fn(vm, arr_is_array)));
    vm->global_object()->set("Array_from", JSValue::function(make_fn(vm, arr_from, false, ctx)));
    vm->global_object()->set("Array_of", JSValue::function(make_fn(vm, arr_of, false, ctx)));
}

} // namespace browser::js::builtins
