#pragma once
#include "../value.hpp"
#include "../vm.hpp"
#include "../gc.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <cwctype>

namespace browser::js::builtins {

// Register all builtin groups
void register_string_prototype(VM* vm);
void register_array_prototype(VM* vm);
void register_object_builtins(VM* vm);
void register_math_builtins(VM* vm);
void register_number_builtins(VM* vm);
void register_symbol_builtins(VM* vm);
void register_json_builtins(VM* vm);
void register_date_builtins(VM* vm);
void register_regexp_builtins(VM* vm);
void register_error_builtins(VM* vm);
void register_console_builtins(VM* vm);
void register_timer_builtins(VM* vm);
void register_promise_builtins(VM* vm);
void register_performance_builtins(VM* vm);

// Helper: create a native function
inline JSFunction* make_fn(VM* vm, JSFunction::NativeFn fn, bool is_constructor = false, void* ctx = nullptr) {
    return vm->create_native_fn(fn, is_constructor, ctx);
}

// Helper: create an object with a given prototype
inline JSObject* create_object_with_proto(VM* vm, JSValue proto) {
    auto* gc_obj = vm->heap()->alloc_object();
    gc_obj->obj.prototype = proto;
    return &gc_obj->obj;
}

// Helper: get string argument or convert to string
inline std::string get_string_arg(const std::vector<JSValue>& args, u32 idx) {
    if (idx >= args.size()) return "";
    return args[idx].to_string();
}

// Helper: get numeric argument or default
inline f64 get_number_arg(const std::vector<JSValue>& args, u32 idx, f64 default_val = 0.0) {
    if (idx >= args.size()) return default_val;
    return args[idx].to_number();
}

// Helper: get int argument
inline i32 get_int_arg(const std::vector<JSValue>& args, u32 idx, i32 default_val = 0) {
    if (idx >= args.size()) return default_val;
    return static_cast<i32>(args[idx].to_number());
}

// Helper: check if a value is an array
inline bool is_array(const JSValue& val) {
    return val.type == JSValue::Type::OBJECT && val.object_val && val.object_val->is_array;
}

// Helper: get array elements
inline std::vector<JSValue>& get_array_elements(const JSValue& val) {
    return val.object_val->array_elements;
}

// Helper: to integer index
inline u32 to_uint32(f64 val) {
    if (std::isnan(val) || std::isinf(val)) return 0;
    if (val < 0) return 0;
    return static_cast<u32>(val);
}

// Helper: clamp
inline f64 clamp(f64 val, f64 min, f64 max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

// Helper: setup a prototype object with native methods
inline void set_prototype_method(JSObject* proto, const std::string& name, JSFunction* fn) {
    proto->set(name, JSValue::function(fn));
}

} // namespace browser::js::builtins
