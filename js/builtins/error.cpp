#include "builtins.hpp"
#include <sstream>

namespace browser::js::builtins {

struct ErrCtx { VM* vm; };

static JSValue error_ctor(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<ErrCtx*>(context);
    auto* err_gc = ctx->vm->heap()->alloc_object();
    std::string msg = args.size() > 1 ? args[1].to_string() : "";
    err_gc->obj.set("message", JSValue::string(msg));
    err_gc->obj.set("name", JSValue::string(ctx->vm->global_object()->get("Error").to_string()));
    err_gc->obj.set("stack", JSValue::string("Error: " + msg + "\n    at <eval>"));
    return JSValue::object(&err_gc->obj);
}

static JSValue error_to_string(const std::vector<JSValue>& args, void*) {
    std::string name = "Error";
    std::string msg = args[0].to_string();
    if (args[0].type == JSValue::Type::OBJECT) {
        auto* obj = args[0].object_val;
        if (obj) {
            JSValue n = obj->get_property("name");
            if (n.type == JSValue::Type::STRING) name = n.string_val;
            JSValue m = obj->get_property("message");
            if (m.type == JSValue::Type::STRING) msg = m.string_val;
        }
    }
    return JSValue::string(name + ": " + msg);
}

void register_error_builtins(VM* vm) {
    auto* ctx = new ErrCtx{vm};
    auto* err_proto = vm->heap()->alloc_object();
    err_proto->obj.set("name", JSValue::string("Error"));
    err_proto->obj.set("message", JSValue::string(""));
    set_prototype_method(&err_proto->obj, "toString", make_fn(vm, error_to_string));
    auto* err_ctor = vm->heap()->alloc_object();
    err_ctor->obj.set("prototype", JSValue::object(&err_proto->obj));
    vm->global_object()->set("Error", JSValue::function(make_fn(vm, error_ctor, true, ctx)));
    // Register Error as both function and object
    // Subclasses
    auto make_error_class = [vm](const std::string& name) {
        auto* proto = vm->heap()->alloc_object();
        proto->obj.set("name", JSValue::string(name));
        proto->obj.set("message", JSValue::string(""));
        return proto;
    };
    auto* type_err = make_error_class("TypeError");
    auto* ref_err = make_error_class("ReferenceError");
    auto* syn_err = make_error_class("SyntaxError");
    auto* range_err = make_error_class("RangeError");
    auto* uri_err = make_error_class("URIError");
    auto* eval_err = make_error_class("EvalError");
    vm->global_object()->set("TypeError", JSValue::object(&type_err->obj));
    vm->global_object()->set("ReferenceError", JSValue::object(&ref_err->obj));
    vm->global_object()->set("SyntaxError", JSValue::object(&syn_err->obj));
    vm->global_object()->set("RangeError", JSValue::object(&range_err->obj));
    vm->global_object()->set("URIError", JSValue::object(&uri_err->obj));
    vm->global_object()->set("EvalError", JSValue::object(&eval_err->obj));
}

} // namespace browser::js::builtins
