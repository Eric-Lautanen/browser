#include "builtins.hpp"

#include <sstream>

namespace browser::js::builtins {

    struct ErrCtx {
        VM *vm;
        const char *name;
    };

    static JSValue error_to_string(const std::vector<JSValue> &args, void *) {
        std::string name = "Error";
        std::string msg;
        if (!args.empty() && args[0].type == JSValue::Type::OBJECT && args[0].object_val) {
            auto *obj = args[0].object_val;
            JSValue n = obj->get_property("name");
            if (n.type == JSValue::Type::STRING)
                name = n.string_val;
            JSValue m = obj->get_property("message");
            if (m.type == JSValue::Type::STRING)
                msg = m.string_val;
        }
        if (msg.empty())
            return JSValue::string(name);
        return JSValue::string(name + ": " + msg);
    }

    // Shared constructor for Error and its subclasses. Works with and without
    // `new`: args[0] is `this` when constructed; otherwise a fresh object is made.
    static JSValue error_ctor_impl(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<ErrCtx *>(context);
        VM *vm = ctx->vm;

        GCJSObject *holder = nullptr;
        bool has_this = !args.empty() && args[0].type == JSValue::Type::OBJECT && args[0].object_val;
        if (has_this) {
            // Find the GC wrapper so lifetime is tracked; fall back to raw object.
            holder = vm->heap()->lookup_object(args[0].object_val);
            if (!holder) {
                auto *alloc = vm->heap()->alloc_object();
                alloc->obj.prototype = args[0].object_val->prototype;
                for (const auto &[k, v] : args[0].object_val->properties) alloc->obj.set(k, v);
                holder = alloc;
            }
        } else {
            holder = vm->heap()->alloc_object();
            // Prototype from the global constructor's .prototype property.
            JSValue ctor_val = vm->global_object()->get(ctx->name);
            if (ctor_val.type == JSValue::Type::FUNCTION && ctor_val.function_val) {
                holder->obj.prototype = ctor_val.function_val->prototype_property;
            }
        }

        std::string msg = args.size() > 1 ? args[1].to_string() : "";
        holder->obj.set("message", JSValue::string(msg));
        holder->obj.set("name", JSValue::string(ctx->name));
        holder->obj.set("stack", JSValue::string(std::string(ctx->name) + ": " + msg + "\n    at <eval>"));
        return JSValue::object(&holder->obj);
    }

    void register_error_builtins(VM *vm) {
        auto *err_proto = vm->heap()->alloc_object();
        err_proto->obj.set("name", JSValue::string("Error"));
        err_proto->obj.set("message", JSValue::string(""));
        set_prototype_method(&err_proto->obj, "toString", make_fn(vm, error_to_string));

        static ErrCtx base_ctx{nullptr, "Error"};
        base_ctx.vm = vm;
        auto *err_ctor_fn = make_fn(vm, error_ctor_impl, true, &base_ctx);
        err_ctor_fn->name = "Error";
        err_ctor_fn->prototype_property = JSValue::object(&err_proto->obj);
        err_ctor_fn->properties["prototype"] = JSValue::object(&err_proto->obj);
        err_proto->obj.set("constructor", JSValue::function(err_ctor_fn));
        vm->global_object()->set("Error", JSValue::function(err_ctor_fn));

        // Subclasses get their own prototypes chained onto Error.prototype and are
        // registered as real constructors so `new TypeError(...)` and
        // `e instanceof TypeError` both work.
        static const char *kSubclasses[] = {
            "TypeError", "ReferenceError", "SyntaxError", "RangeError", "URIError", "EvalError"};
        static ErrCtx sub_ctxs[6] = {{}, {}, {}, {}, {}, {}};
        for (int i = 0; i < 6; i++) {
            auto *proto = vm->heap()->alloc_object();
            proto->obj.prototype = JSValue::object(&err_proto->obj);
            proto->obj.set("name", JSValue::string(kSubclasses[i]));
            proto->obj.set("message", JSValue::string(""));

            sub_ctxs[i].vm = vm;
            sub_ctxs[i].name = kSubclasses[i];
            auto *ctor_fn = make_fn(vm, error_ctor_impl, true, &sub_ctxs[i]);
            ctor_fn->name = kSubclasses[i];
            ctor_fn->prototype_property = JSValue::object(&proto->obj);
            ctor_fn->properties["prototype"] = JSValue::object(&proto->obj);
            proto->obj.set("constructor", JSValue::function(ctor_fn));
            vm->global_object()->set(kSubclasses[i], JSValue::function(ctor_fn));
        }
    }

}  // namespace browser::js::builtins
