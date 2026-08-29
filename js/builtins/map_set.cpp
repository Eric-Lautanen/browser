#include "builtins.hpp"

#include <string>
#include <vector>

namespace browser::js::builtins {

    // Map/Set storage lives on the instance under hidden keys as two
    // index-aligned JS arrays (insertion order per spec), so entries stay
    // visible to the GC and survive like ordinary object state.
    static constexpr const char *kKeys = "!mapkeys";
    static constexpr const char *kVals = "!mapvals";

    struct MapCtx {
        VM *vm;
    };

    static JSValue get_hidden_array(const JSValue &obj_val, const char *key) {
        if (obj_val.type != JSValue::Type::OBJECT || !obj_val.object_val)
            return JSValue::undefined();
        JSValue arr = obj_val.object_val->get(key);
        if (arr.type == JSValue::Type::OBJECT && arr.object_val && arr.object_val->is_array)
            return arr;
        return JSValue::undefined();
    }

    // SameValueZero: NaN equals NaN, +0 equals -0, objects compare by identity.
    static bool same_value_zero(const JSValue &a, const JSValue &b) {
        if (a.type != b.type) {
            // NUMBER vs NUMBER handled below; allow int-like equality through
            // the NUMBER path only.
            return false;
        }
        switch (a.type) {
            case JSValue::Type::NUMBER: {
                f64 av = a.number_val, bv = b.number_val;
                if (av != av && bv != bv)
                    return true;  // NaN == NaN
                return av == bv;
            }
            case JSValue::Type::STRING:
                return a.string_val == b.string_val;
            case JSValue::Type::OBJECT:
            case JSValue::Type::FUNCTION:
                return a.object_val == b.object_val;
            default:
                return true;  // undefined/null/bool with equal types
        }
    }

    static i64 find_key(const JSValue &keys_arr, const JSValue &key) {
        if (keys_arr.type != JSValue::Type::OBJECT || !keys_arr.object_val)
            return -1;
        const auto &elems = keys_arr.object_val->array_elements;
        for (size_t i = 0; i < elems.size(); i++) {
            if (same_value_zero(elems[i], key))
                return static_cast<i64>(i);
        }
        return -1;
    }

    static JSValue map_get_storage(JSObject *obj, VM *vm) {
        JSValue keys = obj->get(kKeys);
        if (!(keys.type == JSValue::Type::OBJECT && keys.object_val && keys.object_val->is_array)) {
            auto *kgc = vm->heap()->alloc_object();
            kgc->obj.is_array = true;
            auto *vgc = vm->heap()->alloc_object();
            vgc->obj.is_array = true;
            obj->set(kKeys, JSValue::object(&kgc->obj));
            obj->set(kVals, JSValue::object(&vgc->obj));
            keys = JSValue::object(&kgc->obj);
        }
        return keys;
    }

    static JSValue map_size_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        (void)ctx;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue keys = get_hidden_array(this_val, kKeys);
        if (keys.type != JSValue::Type::OBJECT)
            return JSValue::number(0);
        return JSValue::number(static_cast<f64>(keys.object_val->array_elements.size()));
    }

    static JSValue map_get_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue keys = get_hidden_array(this_val, kKeys);
        JSValue vals = get_hidden_array(this_val, kVals);
        if (keys.type != JSValue::Type::OBJECT || vals.type != JSValue::Type::OBJECT || args.size() < 2)
            return JSValue::undefined();
        i64 idx = find_key(keys, args[1]);
        if (idx < 0 || static_cast<size_t>(idx) >= vals.object_val->array_elements.size())
            return JSValue::undefined();
        (void)vm;
        return vals.object_val->array_elements[static_cast<size_t>(idx)];
    }

    static JSValue map_set_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        if (this_val.type != JSValue::Type::OBJECT || !this_val.object_val || args.size() < 3)
            return this_val;
        JSValue keys = map_get_storage(this_val.object_val, vm);
        JSValue vals = this_val.object_val->get(kVals);
        i64 idx = find_key(keys, args[1]);
        if (idx >= 0) {
            if (static_cast<size_t>(idx) < vals.object_val->array_elements.size())
                vals.object_val->array_elements[static_cast<size_t>(idx)] = args[2];
        } else {
            keys.object_val->array_elements.push_back(args[1]);
            vals.object_val->array_elements.push_back(args[2]);
        }
        // size as a data property, refreshed on mutation
        this_val.object_val->set("size", JSValue::number(static_cast<f64>(keys.object_val->array_elements.size())));
        return this_val;
    }

    static JSValue map_has_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        (void)ctx;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue keys = get_hidden_array(this_val, kKeys);
        if (keys.type != JSValue::Type::OBJECT || args.size() < 2)
            return JSValue::boolean(false);
        return JSValue::boolean(find_key(keys, args[1]) >= 0);
    }

    static JSValue map_delete_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        (void)ctx;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue keys = get_hidden_array(this_val, kKeys);
        JSValue vals = get_hidden_array(this_val, kVals);
        if (keys.type != JSValue::Type::OBJECT || vals.type != JSValue::Type::OBJECT || args.size() < 2)
            return JSValue::boolean(false);
        i64 idx = find_key(keys, args[1]);
        if (idx < 0)
            return JSValue::boolean(false);
        auto &ke = keys.object_val->array_elements;
        auto &ve = vals.object_val->array_elements;
        ke.erase(ke.begin() + idx);
        if (static_cast<size_t>(idx) < ve.size())
            ve.erase(ve.begin() + idx);
        this_val.object_val->set("size", JSValue::number(static_cast<f64>(ke.size())));
        return JSValue::boolean(true);
    }

    static JSValue map_clear_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        if (this_val.type != JSValue::Type::OBJECT || !this_val.object_val)
            return JSValue::undefined();
        JSValue keys = map_get_storage(this_val.object_val, vm);
        JSValue vals = this_val.object_val->get(kVals);
        keys.object_val->array_elements.clear();
        if (vals.type == JSValue::Type::OBJECT)
            vals.object_val->array_elements.clear();
        this_val.object_val->set("size", JSValue::number(0));
        return JSValue::undefined();
    }

    static JSValue map_for_each_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue keys = get_hidden_array(this_val, kKeys);
        JSValue vals = get_hidden_array(this_val, kVals);
        if (keys.type != JSValue::Type::OBJECT || vals.type != JSValue::Type::OBJECT || args.size() < 2 ||
            args[1].type != JSValue::Type::FUNCTION)
            return JSValue::undefined();
        // Snapshot: spec allows deletion during iteration; iterate a copy of
        // the entries present at call time.
        auto key_copy = keys.object_val->array_elements;
        auto val_copy = vals.object_val->array_elements;
        for (size_t i = 0; i < key_copy.size() && i < val_copy.size(); i++) {
            std::vector<JSValue> cb_args = {JSValue::undefined(), val_copy[i], key_copy[i], this_val};
            vm->invoke(JSValue::function(args[1].function_val), cb_args, JSValue::undefined());
        }
        return JSValue::undefined();
    }

    // Shared iterator-style function returning an array of keys or values
    // (arrays are iterable enough for spread/for..of once implemented; code
    // commonly uses .forEach or index access, both of which work).
    static JSValue collection_values_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue keys = get_hidden_array(this_val, kKeys);
        JSValue vals = get_hidden_array(this_val, kVals);
        auto *arr = vm->heap()->alloc_object();
        arr->obj.is_array = true;
        if (keys.type == JSValue::Type::OBJECT)
            arr->obj.array_elements = keys.object_val->array_elements;
        (void)vals;
        return JSValue::object(&arr->obj);
    }

    static JSValue map_entries_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue keys = get_hidden_array(this_val, kKeys);
        JSValue vals = get_hidden_array(this_val, kVals);
        auto *arr = vm->heap()->alloc_object();
        arr->obj.is_array = true;
        if (keys.type == JSValue::Type::OBJECT && vals.type == JSValue::Type::OBJECT) {
            auto &ke = keys.object_val->array_elements;
            auto &ve = vals.object_val->array_elements;
            for (size_t i = 0; i < ke.size() && i < ve.size(); i++) {
                auto *pair = vm->heap()->alloc_object();
                pair->obj.is_array = true;
                pair->obj.array_elements.push_back(ke[i]);
                pair->obj.array_elements.push_back(ve[i]);
                arr->obj.array_elements.push_back(JSValue::object(&pair->obj));
            }
        }
        return JSValue::object(&arr->obj);
    }

    // ---------------- Set ----------------

    static JSValue set_add_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        if (this_val.type != JSValue::Type::OBJECT || !this_val.object_val || args.size() < 2)
            return this_val;
        JSValue vals = map_get_storage(this_val.object_val, vm);
        if (find_key(vals, args[1]) < 0) {
            vals.object_val->array_elements.push_back(args[1]);
            this_val.object_val->set("size", JSValue::number(static_cast<f64>(vals.object_val->array_elements.size())));
        }
        return this_val;
    }

    static JSValue set_has_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        (void)ctx;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue vals = get_hidden_array(this_val, kKeys);
        if (vals.type != JSValue::Type::OBJECT || args.size() < 2)
            return JSValue::boolean(false);
        return JSValue::boolean(find_key(vals, args[1]) >= 0);
    }

    static JSValue set_delete_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        (void)ctx;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue vals = get_hidden_array(this_val, kKeys);
        if (vals.type != JSValue::Type::OBJECT || args.size() < 2)
            return JSValue::boolean(false);
        i64 idx = find_key(vals, args[1]);
        if (idx < 0)
            return JSValue::boolean(false);
        auto &ve = vals.object_val->array_elements;
        ve.erase(ve.begin() + idx);
        this_val.object_val->set("size", JSValue::number(static_cast<f64>(ve.size())));
        return JSValue::boolean(true);
    }

    static JSValue set_clear_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        if (this_val.type != JSValue::Type::OBJECT || !this_val.object_val)
            return JSValue::undefined();
        JSValue vals = map_get_storage(this_val.object_val, vm);
        vals.object_val->array_elements.clear();
        this_val.object_val->set("size", JSValue::number(0));
        return JSValue::undefined();
    }

    static JSValue set_for_each_fn(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue this_val = args.empty() ? JSValue::undefined() : args[0];
        JSValue vals = get_hidden_array(this_val, kKeys);
        if (vals.type != JSValue::Type::OBJECT || args.size() < 2 || args[1].type != JSValue::Type::FUNCTION)
            return JSValue::undefined();
        auto val_copy = vals.object_val->array_elements;
        for (const auto &v : val_copy) {
            std::vector<JSValue> cb_args = {JSValue::undefined(), v, v, this_val};
            vm->invoke(JSValue::function(args[1].function_val), cb_args, JSValue::undefined());
        }
        return JSValue::undefined();
    }

    // ---------------- constructors ----------------

    static JSValue map_constructor(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue ctor_val = vm->global_object()->get("Map");
        JSValue proto = JSValue::undefined();
        if (ctor_val.type == JSValue::Type::FUNCTION && ctor_val.function_val &&
            ctor_val.function_val->prototype_property.type == JSValue::Type::OBJECT)
            proto = ctor_val.function_val->prototype_property;

        auto *obj_gc = vm->heap()->alloc_object();
        auto &obj = obj_gc->obj;
        obj.prototype = proto;
        obj.set(kKeys, JSValue::undefined());
        obj.set(kVals, JSValue::undefined());
        map_get_storage(&obj, vm);
        obj.set("size", JSValue::number(0));

        JSValue self = JSValue::object(&obj);
        // Optional iterable of [key, value] pairs.
        if (args.size() >= 2 && args[1].type == JSValue::Type::OBJECT && args[1].object_val &&
            args[1].object_val->is_array) {
            for (const auto &entry : args[1].object_val->array_elements) {
                if (entry.type == JSValue::Type::OBJECT && entry.object_val && entry.object_val->is_array &&
                    entry.object_val->array_elements.size() >= 2) {
                    std::vector<JSValue> set_args = {
                        self, entry.object_val->array_elements[0], entry.object_val->array_elements[1]};
                    map_set_fn(set_args, context);
                }
            }
        }
        return self;
    }

    static JSValue set_constructor(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<MapCtx *>(context);
        VM *vm = ctx->vm;
        JSValue ctor_val = vm->global_object()->get("Set");
        JSValue proto = JSValue::undefined();
        if (ctor_val.type == JSValue::Type::FUNCTION && ctor_val.function_val &&
            ctor_val.function_val->prototype_property.type == JSValue::Type::OBJECT)
            proto = ctor_val.function_val->prototype_property;

        auto *obj_gc = vm->heap()->alloc_object();
        auto &obj = obj_gc->obj;
        obj.prototype = proto;
        obj.set(kKeys, JSValue::undefined());
        map_get_storage(&obj, vm);
        obj.set("size", JSValue::number(0));

        JSValue self = JSValue::object(&obj);
        if (args.size() >= 2 && args[1].type == JSValue::Type::OBJECT && args[1].object_val &&
            args[1].object_val->is_array) {
            for (const auto &v : args[1].object_val->array_elements) {
                std::vector<JSValue> add_args = {self, v};
                set_add_fn(add_args, context);
            }
        }
        return self;
    }

    void register_map_set_builtins(VM *vm) {
        auto *ctx = new MapCtx{vm};

        {
            auto *map_proto = vm->heap()->alloc_object();
            set_prototype_method(&map_proto->obj, "get", make_fn(vm, map_get_fn, false, ctx));
            set_prototype_method(&map_proto->obj, "set", make_fn(vm, map_set_fn, false, ctx));
            set_prototype_method(&map_proto->obj, "has", make_fn(vm, map_has_fn, false, ctx));
            set_prototype_method(&map_proto->obj, "delete", make_fn(vm, map_delete_fn, false, ctx));
            set_prototype_method(&map_proto->obj, "clear", make_fn(vm, map_clear_fn, false, ctx));
            set_prototype_method(&map_proto->obj, "forEach", make_fn(vm, map_for_each_fn, false, ctx));
            set_prototype_method(&map_proto->obj, "keys", make_fn(vm, collection_values_fn, false, ctx));
            set_prototype_method(&map_proto->obj, "values", make_fn(vm, collection_values_fn, false, ctx));
            set_prototype_method(&map_proto->obj, "entries", make_fn(vm, map_entries_fn, false, ctx));

            auto *map_fn = vm->create_native_fn(map_constructor, true, ctx);
            map_fn->name = "Map";
            map_fn->prototype_property = JSValue::object(&map_proto->obj);
            map_fn->properties["prototype"] = JSValue::object(&map_proto->obj);
            // size is a data property on instances, refreshed by mutators;
            // expose size() on the prototype as a fallback for parity.
            set_prototype_method(&map_proto->obj, "size", make_fn(vm, map_size_fn, false, ctx));
            vm->global_object()->set("Map", JSValue::function(map_fn));
        }

        {
            auto *set_proto = vm->heap()->alloc_object();
            set_prototype_method(&set_proto->obj, "add", make_fn(vm, set_add_fn, false, ctx));
            set_prototype_method(&set_proto->obj, "has", make_fn(vm, set_has_fn, false, ctx));
            set_prototype_method(&set_proto->obj, "delete", make_fn(vm, set_delete_fn, false, ctx));
            set_prototype_method(&set_proto->obj, "clear", make_fn(vm, set_clear_fn, false, ctx));
            set_prototype_method(&set_proto->obj, "forEach", make_fn(vm, set_for_each_fn, false, ctx));
            set_prototype_method(&set_proto->obj, "values", make_fn(vm, collection_values_fn, false, ctx));
            set_prototype_method(&set_proto->obj, "keys", make_fn(vm, collection_values_fn, false, ctx));

            auto *set_fn = vm->create_native_fn(set_constructor, true, ctx);
            set_fn->name = "Set";
            set_fn->prototype_property = JSValue::object(&set_proto->obj);
            set_fn->properties["prototype"] = JSValue::object(&set_proto->obj);
            vm->global_object()->set("Set", JSValue::function(set_fn));
        }
    }

}  // namespace browser::js::builtins
