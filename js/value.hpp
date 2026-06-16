#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <cstdlib>
#include "../tests/utility.hpp"

namespace browser::js {

struct BytecodeFunction;
struct JSObject;
struct JSFunction;

struct JSValue {
    enum class Type { UNDEFINED, NULL_VAL, BOOLEAN, NUMBER, STRING, OBJECT, FUNCTION };
    Type type = Type::UNDEFINED;
    bool bool_val = false;
    f64 number_val = 0;
    std::string string_val;
    JSObject* object_val = nullptr;
    JSFunction* function_val = nullptr;

    bool is_truthy() const;
    std::string to_string() const;
    f64 to_number() const;

    static JSValue undefined() { return {}; }
    static JSValue null() { JSValue v; v.type = Type::NULL_VAL; return v; }
    static JSValue number(f64 v) { JSValue val; val.type = Type::NUMBER; val.number_val = v; return val; }
    static JSValue boolean(bool v) { JSValue val; val.type = Type::BOOLEAN; val.bool_val = v; return val; }
    static JSValue string(const std::string& v) { JSValue val; val.type = Type::STRING; val.string_val = v; return val; }
    static JSValue object(JSObject* o) { JSValue val; val.type = Type::OBJECT; val.object_val = o; return val; }
    static JSValue function(JSFunction* f) { JSValue val; val.type = Type::FUNCTION; val.function_val = f; return val; }
};

struct JSObject {
    std::unordered_map<std::string, JSValue> properties;
    JSValue prototype;
    bool is_array = false;
    std::vector<JSValue> array_elements;
    JSValue get(const std::string& name) const;
    void set(const std::string& name, const JSValue& val);
};

struct JSFunction {
    BytecodeFunction* bytecode = nullptr;
    using NativeFn = JSValue(*)(const std::vector<JSValue>& args, void* context);
    NativeFn native_fn = nullptr;
    void* native_context = nullptr;
    std::string name;
    bool is_constructor = false;
};

} // namespace browser::js
