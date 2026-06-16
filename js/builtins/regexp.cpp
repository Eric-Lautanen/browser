#include "builtins.hpp"

namespace browser::js::builtins {

struct RegexpCtx { VM* vm; };

static JSValue regexp_test(const std::vector<JSValue>& args, void*) {
    std::string pattern = args[0].to_string();
    std::string text = args.size() > 1 ? args[1].to_string() : "";
    // Simple substring match (no real regex engine)
    bool found = text.find(pattern) != std::string::npos;
    return JSValue::boolean(found);
}

static JSValue regexp_exec(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<RegexpCtx*>(context);
    std::string pattern = args[0].to_string();
    std::string text = args.size() > 1 ? args[1].to_string() : "";
    auto pos = text.find(pattern);
    if (pos == std::string::npos) return JSValue::null();
    auto* arr_gc = ctx->vm->heap()->alloc_object();
    arr_gc->obj.is_array = true;
    arr_gc->obj.array_elements.push_back(JSValue::string(text.substr(pos, pattern.size())));
    arr_gc->obj.set("index", JSValue::number(static_cast<f64>(pos)));
    arr_gc->obj.set("input", JSValue::string(text));
    return JSValue::object(&arr_gc->obj);
}

void register_regexp_builtins(VM* vm) {
    auto* ctx = new RegexpCtx{vm};
    auto* regexp_proto = vm->heap()->alloc_object();
    set_prototype_method(&regexp_proto->obj, "test", make_fn(vm, regexp_test));
    set_prototype_method(&regexp_proto->obj, "exec", make_fn(vm, regexp_exec, false, ctx));
    auto* regexp_ctor = vm->heap()->alloc_object();
    regexp_ctor->obj.set("prototype", JSValue::object(&regexp_proto->obj));
    vm->global_object()->set("RegExp", JSValue::object(&regexp_ctor->obj));
}

} // namespace browser::js::builtins
