#include "builtins.hpp"

namespace browser::js::builtins {

struct SymCtx { VM* vm; };

static JSValue symbol_for(const std::vector<JSValue>& args, void*) {
    std::string key = args.size() > 1 ? args[1].to_string() : "undefined";
    return JSValue::string("Symbol(" + key + ")");
}

static JSValue symbol_key_for(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::undefined();
    return JSValue::string(args[1].to_string());
}

void register_symbol_builtins(VM* vm) {
    auto* sym_ctor = vm->heap()->alloc_object();
    sym_ctor->obj.set("for", JSValue::function(make_fn(vm, symbol_for)));
    sym_ctor->obj.set("keyFor", JSValue::function(make_fn(vm, symbol_key_for)));
    sym_ctor->obj.set("iterator", JSValue::string("Symbol.iterator"));
    sym_ctor->obj.set("toStringTag", JSValue::string("Symbol.toStringTag"));
    vm->global_object()->set("Symbol", JSValue::object(&sym_ctor->obj));
}

} // namespace browser::js::builtins
