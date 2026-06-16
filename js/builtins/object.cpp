#include "builtins.hpp"

namespace browser::js::builtins {

struct ObjCtx { VM* vm; };

static JSValue obj_keys(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ObjCtx*>(context);
    auto* arr_gc = ctx->vm->heap()->alloc_object();
    arr_gc->obj.is_array = true;
    auto& result = arr_gc->obj.array_elements;
    if (args.size() > 1 && args[1].type == JSValue::Type::OBJECT) {
        for (auto& [k, v] : args[1].object_val->properties) {
            (void)v;
            result.push_back(JSValue::string(k));
        }
    }
    return JSValue::object(&arr_gc->obj);
}

static JSValue obj_values(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ObjCtx*>(context);
    auto* arr_gc = ctx->vm->heap()->alloc_object();
    arr_gc->obj.is_array = true;
    auto& result = arr_gc->obj.array_elements;
    if (args.size() > 1 && args[1].type == JSValue::Type::OBJECT) {
        for (auto& [k, v] : args[1].object_val->properties) {
            (void)k;
            result.push_back(v);
        }
    }
    return JSValue::object(&arr_gc->obj);
}

static JSValue obj_entries(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ObjCtx*>(context);
    auto* arr_gc = ctx->vm->heap()->alloc_object();
    arr_gc->obj.is_array = true;
    auto& result = arr_gc->obj.array_elements;
    if (args.size() > 1 && args[1].type == JSValue::Type::OBJECT) {
        for (auto& [k, v] : args[1].object_val->properties) {
            auto* entry = ctx->vm->heap()->alloc_object();
            entry->obj.is_array = true;
            entry->obj.array_elements.push_back(JSValue::string(k));
            entry->obj.array_elements.push_back(v);
            result.push_back(JSValue::object(&entry->obj));
        }
    }
    return JSValue::object(&arr_gc->obj);
}

static JSValue obj_assign(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT) return args.size() > 1 ? args[1] : JSValue::undefined();
    JSValue target = args[1];
    for (u32 i = 2; i < args.size(); i++) {
        if (args[i].type == JSValue::Type::OBJECT) {
            for (auto& [k, v] : args[i].object_val->properties) {
                target.object_val->set(k, v);
            }
        }
    }
    return target;
}

static JSValue obj_create(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ObjCtx*>(context);
    auto* obj_gc = ctx->vm->heap()->alloc_object();
    JSObject* obj = &obj_gc->obj;
    if (args.size() > 1 && args[1].type == JSValue::Type::OBJECT) {
        obj->prototype = args[1];
    } else if (args.size() > 1 && args[1].type == JSValue::Type::NULL_VAL) {
        obj->prototype = JSValue::null();
    }
    if (args.size() > 2 && args[2].type == JSValue::Type::OBJECT) {
        for (auto& [k, v] : args[2].object_val->properties) {
            obj->set(k, v);
        }
    }
    return JSValue::object(obj);
}

static JSValue obj_get_proto(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT) return JSValue::undefined();
    return args[1].object_val->prototype;
}

static JSValue obj_set_proto(const std::vector<JSValue>& args, void*) {
    if (args.size() < 3 || args[1].type != JSValue::Type::OBJECT) return JSValue::undefined();
    args[1].object_val->prototype = args[2];
    return JSValue::undefined();
}

static JSValue obj_freeze(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT) return args.size() > 1 ? args[1] : JSValue::undefined();
    args[1].object_val->is_frozen = true;
    args[1].object_val->is_extensible = false;
    return args[1];
}

static JSValue obj_seal(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT) return args.size() > 1 ? args[1] : JSValue::undefined();
    args[1].object_val->is_sealed = true;
    args[1].object_val->is_extensible = false;
    return args[1];
}

static JSValue obj_prevent_ext(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT) return args.size() > 1 ? args[1] : JSValue::undefined();
    args[1].object_val->is_extensible = false;
    return args[1];
}

static JSValue obj_is_frozen(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT) return JSValue::boolean(true);
    return JSValue::boolean(args[1].object_val->is_frozen);
}

static JSValue obj_is_sealed(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT) return JSValue::boolean(true);
    return JSValue::boolean(args[1].object_val->is_sealed);
}

static JSValue obj_is_ext(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT) return JSValue::boolean(false);
    return JSValue::boolean(args[1].object_val->is_extensible);
}

static JSValue obj_get_own_prop_names(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ObjCtx*>(context);
    auto* arr_gc = ctx->vm->heap()->alloc_object();
    arr_gc->obj.is_array = true;
    auto& result = arr_gc->obj.array_elements;
    if (args.size() > 1 && args[1].type == JSValue::Type::OBJECT) {
        for (auto& [k, v] : args[1].object_val->properties) {
            (void)v;
            result.push_back(JSValue::string(k));
        }
    }
    return JSValue::object(&arr_gc->obj);
}

void register_object_builtins(VM* vm) {
    auto* ctx = new ObjCtx{vm};
    auto* obj_ctor = vm->heap()->alloc_object();
    obj_ctor->obj.set("keys", JSValue::function(make_fn(vm, obj_keys, false, ctx)));
    obj_ctor->obj.set("values", JSValue::function(make_fn(vm, obj_values, false, ctx)));
    obj_ctor->obj.set("entries", JSValue::function(make_fn(vm, obj_entries, false, ctx)));
    obj_ctor->obj.set("assign", JSValue::function(make_fn(vm, obj_assign)));
    obj_ctor->obj.set("create", JSValue::function(make_fn(vm, obj_create, false, ctx)));
    obj_ctor->obj.set("getPrototypeOf", JSValue::function(make_fn(vm, obj_get_proto)));
    obj_ctor->obj.set("setPrototypeOf", JSValue::function(make_fn(vm, obj_set_proto)));
    obj_ctor->obj.set("freeze", JSValue::function(make_fn(vm, obj_freeze)));
    obj_ctor->obj.set("seal", JSValue::function(make_fn(vm, obj_seal)));
    obj_ctor->obj.set("preventExtensions", JSValue::function(make_fn(vm, obj_prevent_ext)));
    obj_ctor->obj.set("isFrozen", JSValue::function(make_fn(vm, obj_is_frozen)));
    obj_ctor->obj.set("isSealed", JSValue::function(make_fn(vm, obj_is_sealed)));
    obj_ctor->obj.set("isExtensible", JSValue::function(make_fn(vm, obj_is_ext)));
    obj_ctor->obj.set("getOwnPropertyNames", JSValue::function(make_fn(vm, obj_get_own_prop_names, false, ctx)));
    // Object.prototype
    auto* obj_proto = vm->heap()->alloc_object();
    obj_ctor->obj.set("prototype", JSValue::object(&obj_proto->obj));
    vm->global_object()->set("Object", JSValue::object(&obj_ctor->obj));
}

} // namespace browser::js::builtins
