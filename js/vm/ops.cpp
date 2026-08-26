#include "gc.hpp"
#include "vm.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace browser::js {

    // ---------------------------------------------------------------------------
    // JSValue / JSObject methods
    // ---------------------------------------------------------------------------

    // J-C7: shortest round-trip formatting per the ECMAScript number-to-string
    // algorithm. std::to_string produced fixed six decimals (0.1+0.2 printed
    // "0.300000"), losing precision and diverging from JS everywhere strings
    // are built. std::to_chars yields the shortest representation that
    // round-trips; we only normalize the exponent form to JS rules.
    static std::string format_number(f64 val) {
        if (std::isnan(val))
            return "NaN";
        if (std::isinf(val))
            return val > 0 ? "Infinity" : "-Infinity";
        if (val == 0)
            return "0";

        char buf[64];
        auto res = std::to_chars(buf, buf + sizeof(buf), val);
        std::string s(buf, res.ptr);

        const bool negative = !s.empty() && s[0] == '-';
        const std::string body = negative ? s.substr(1) : s;

        const std::size_t epos = body.find('e');
        if (epos == std::string::npos) {
            // Fixed notation: to_chars already emits the shortest digits.
            return s;
        }

        // Scientific: d[.ddd]e±X — decide between fixed and exponential using
        // the JS thresholds. With n = exponent + 1 (digits before the point),
        // the spec uses plain notation while -6 < n <= 21.
        const std::string mantissa = body.substr(0, epos);
        int exponent = std::atoi(body.c_str() + epos + 1);

        std::string digits;
        for (char ch : mantissa)
            if (ch != '.')
                digits += ch;
        // Strip trailing zeros of the digit string (to_chars shouldn't emit
        // them, but be defensive).
        while (digits.size() > 1 && digits.back() == '0') digits.pop_back();

        if (exponent >= -6 && exponent < 21) {
            // Expand to plain decimal notation.
            std::string out;
            const int point = exponent + 1;  // digits before the decimal point
            if (point <= 0) {
                out = "0.";
                out.append(static_cast<std::size_t>(-point), '0');
                out += digits;
            } else if (static_cast<std::size_t>(point) >= digits.size()) {
                out = digits;
                out.append(static_cast<std::size_t>(point) - digits.size(), '0');
            } else {
                out = digits.substr(0, static_cast<std::size_t>(point));
                out += '.';
                out += digits.substr(static_cast<std::size_t>(point));
            }
            return negative ? "-" + out : out;
        }

        // Exponential form with explicit sign and no leading zeros.
        std::string out = digits.substr(0, 1);
        if (digits.size() > 1)
            out += "." + digits.substr(1);
        out += "e";
        out += exponent < 0 ? '-' : '+';
        out += std::to_string(exponent < 0 ? -exponent : exponent);
        return negative ? "-" + out : out;
    }

    bool JSValue::is_truthy() const {
        switch (type) {
            case Type::UNDEFINED:
                return false;
            case Type::NULL_VAL:
                return false;
            case Type::BOOLEAN:
                return bool_val;
            case Type::NUMBER:
                return number_val != 0 && !std::isnan(number_val);
            case Type::STRING:
                return !string_val.empty();
            case Type::OBJECT:
                return true;
            case Type::FUNCTION:
                return true;
            default:
                return true;
        }
    }

    std::string JSValue::to_string() const {
        switch (type) {
            case Type::NUMBER:
                return format_number(number_val);
            case Type::BOOLEAN:
                return bool_val ? "true" : "false";
            case Type::STRING:
                return string_val;
            case Type::UNDEFINED:
                return "undefined";
            case Type::NULL_VAL:
                return "null";
            case Type::OBJECT:
                return "[object Object]";
            case Type::FUNCTION:
                return "[object Object]";
            default:
                return "";
        }
    }

    f64 JSValue::to_number() const {
        switch (type) {
            case Type::NUMBER:
                return number_val;
            case Type::STRING:
                return std::strtod(string_val.c_str(), nullptr);
            case Type::BOOLEAN:
                return bool_val ? 1.0 : 0.0;
            case Type::UNDEFINED:
                return NAN;
            case Type::NULL_VAL:
                return 0.0;
            case Type::OBJECT:
                return NAN;
            case Type::FUNCTION:
                return NAN;
            default:
                return 0;
        }
    }

    JSValue JSObject::get(const std::string &name) const {
        auto it = properties.find(name);
        if (it != properties.end())
            return it->second;
        return JSValue::undefined();
    }

    JSValue JSObject::get_property(const std::string &name) const {
        auto it = properties.find(name);
        if (it != properties.end())
            return it->second;
        // Array index and length access read from array_elements.
        if (is_array) {
            if (name == "length")
                return JSValue::number(static_cast<f64>(array_elements.size()));
            char *end = nullptr;
            long idx = std::strtol(name.c_str(), &end, 10);
            bool numeric = end && !name.empty() && *end == '\0' && idx >= 0;
            if (numeric) {
                size_t i = static_cast<size_t>(idx);
                if (i < array_elements.size())
                    return array_elements[i];
                return JSValue::undefined();
            }
        }
        if (prototype.type == JSValue::Type::OBJECT && prototype.object_val) {
            return prototype.object_val->get_property(name);
        }
        return JSValue::undefined();
    }

    void JSObject::set_property(const std::string &name, const JSValue &val) {
        // Numeric writes on arrays go to storage; holes are filled with undefined.
        if (is_array) {
            char *end = nullptr;
            long idx = std::strtol(name.c_str(), &end, 10);
            bool numeric = end && !name.empty() && *end == '\0' && idx >= 0;
            if (numeric) {
                size_t i = static_cast<size_t>(idx);
                if (i >= array_elements.size())
                    array_elements.resize(i + 1, JSValue::undefined());
                array_elements[i] = val;
                return;
            }
        }
        properties[name] = val;
    }

    bool JSObject::del_property(const std::string &name) {
        // J-C6: `delete obj.prop`.
        return properties.erase(name) > 0;
    }

    void JSObject::set(const std::string &name, const JSValue &val) {
        properties[name] = val;
    }

    bool JSObject::prototype_chain_contains(JSValue obj, JSValue proto) {
        if (obj.type != JSValue::Type::OBJECT || !obj.object_val)
            return false;
        JSValue current = obj.object_val->prototype;
        while (current.type == JSValue::Type::OBJECT && current.object_val) {
            if (current.object_val == proto.object_val)
                return true;
            current = current.object_val->prototype;
        }
        return false;
    }

    // ---------------------------------------------------------------------------
    // VM arithmetic / comparison helpers
    // ---------------------------------------------------------------------------

    JSValue VM::add(const JSValue &a, const JSValue &b) {
        if (a.type == JSValue::Type::STRING || b.type == JSValue::Type::STRING) {
            return JSValue::string(a.to_string() + b.to_string());
        }
        return JSValue::number(a.to_number() + b.to_number());
    }

    JSValue VM::strict_eq(const JSValue &a, const JSValue &b) {
        if (a.type != b.type)
            return JSValue::boolean(false);
        switch (a.type) {
            case JSValue::Type::NUMBER:
                if (std::isnan(a.number_val))
                    return JSValue::boolean(false);
                return JSValue::boolean(a.number_val == b.number_val);
            case JSValue::Type::STRING:
                return JSValue::boolean(a.string_val == b.string_val);
            case JSValue::Type::BOOLEAN:
                return JSValue::boolean(a.bool_val == b.bool_val);
            case JSValue::Type::OBJECT:
                return JSValue::boolean(a.object_val == b.object_val);
            case JSValue::Type::FUNCTION:
                return JSValue::boolean(a.function_val == b.function_val);
            default:
                return JSValue::boolean(true);
        }
    }

    JSValue VM::loose_eq(const JSValue &a, const JSValue &b) {
        if (a.type == b.type)
            return strict_eq(a, b);
        if ((a.type == JSValue::Type::NULL_VAL && b.type == JSValue::Type::UNDEFINED) ||
            (a.type == JSValue::Type::UNDEFINED && b.type == JSValue::Type::NULL_VAL))
            return JSValue::boolean(true);
        if (a.type == JSValue::Type::STRING && b.type == JSValue::Type::NUMBER)
            return loose_eq(JSValue::number(a.to_number()), b);
        if (a.type == JSValue::Type::NUMBER && b.type == JSValue::Type::STRING)
            return loose_eq(a, JSValue::number(b.to_number()));
        if (a.type == JSValue::Type::BOOLEAN)
            return loose_eq(JSValue::number(a.to_number()), b);
        if (b.type == JSValue::Type::BOOLEAN)
            return loose_eq(a, JSValue::number(b.to_number()));
        return JSValue::boolean(false);
    }

    // ---------------------------------------------------------------------------
    // Extracted opcode helpers
    // ---------------------------------------------------------------------------

    void VM::op_add() {
        auto b = pop();
        auto a = pop();
        push(add(a, b));
    }

    void VM::op_sub() {
        auto b = pop();
        auto a = pop();
        push(JSValue::number(a.to_number() - b.to_number()));
    }

    void VM::op_mul() {
        auto b = pop();
        auto a = pop();
        push(JSValue::number(a.to_number() * b.to_number()));
    }

    void VM::op_div() {
        auto b = pop();
        auto a = pop();
        f64 denom = b.to_number();
        if (denom == 0) {
            f64 num = a.to_number();
            push(JSValue::number(num == 0 ? NAN : (num > 0 ? INFINITY : -INFINITY)));
        } else {
            push(JSValue::number(a.to_number() / denom));
        }
    }

    void VM::op_mod() {
        auto b = pop();
        auto a = pop();
        push(JSValue::number(std::fmod(a.to_number(), b.to_number())));
    }

    void VM::op_eq() {
        auto b = pop();
        auto a = pop();
        push(loose_eq(a, b));
    }

    void VM::op_neq() {
        auto b = pop();
        auto a = pop();
        push(JSValue::boolean(!loose_eq(a, b).bool_val));
    }

    void VM::op_strict_eq() {
        auto b = pop();
        auto a = pop();
        push(strict_eq(a, b));
    }

    void VM::op_strict_neq() {
        auto b = pop();
        auto a = pop();
        push(JSValue::boolean(!strict_eq(a, b).bool_val));
    }

    void VM::op_lt() {
        auto b = pop();
        auto a = pop();
        push(JSValue::boolean(a.to_number() < b.to_number()));
    }

    void VM::op_gt() {
        auto b = pop();
        auto a = pop();
        push(JSValue::boolean(a.to_number() > b.to_number()));
    }

    void VM::op_lte() {
        auto b = pop();
        auto a = pop();
        push(JSValue::boolean(a.to_number() <= b.to_number()));
    }

    void VM::op_gte() {
        auto b = pop();
        auto a = pop();
        push(JSValue::boolean(a.to_number() >= b.to_number()));
    }

    // J-C2: prototype for primitive receivers so "abc".trim(), (5).toFixed()
    // and friends resolve through String/Number/Boolean.prototype.
    JSValue VM::primitive_prototype_for(const JSValue &v) const {
        const char *ctor_name = nullptr;
        if (v.type == JSValue::Type::STRING)
            ctor_name = "String";
        else if (v.type == JSValue::Type::NUMBER)
            ctor_name = "Number";
        else if (v.type == JSValue::Type::BOOLEAN)
            ctor_name = "Boolean";
        if (!ctor_name)
            return JSValue::undefined();
        JSValue ctor = global_->obj.get(ctor_name);
        if (ctor.type == JSValue::Type::FUNCTION && ctor.function_val &&
            ctor.function_val->prototype_property.type == JSValue::Type::OBJECT)
            return ctor.function_val->prototype_property;
        return JSValue::undefined();
    }

    void VM::op_get_prop(const std::string &prop) {
        auto obj_val = pop();
        if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val) {
            push(obj_val.object_val->get_property(prop));
        } else if (obj_val.type == JSValue::Type::FUNCTION && obj_val.function_val) {
            // Statics like Date.now / Promise.resolve live on the function
            // itself; "prototype" resolves to the constructor's prototype.
            auto it = obj_val.function_val->properties.find(prop);
            if (it != obj_val.function_val->properties.end()) {
                push(it->second);
            } else if (prop == "prototype") {
                push(obj_val.function_val->prototype_property);
            } else {
                push(JSValue::undefined());
            }
        } else if (obj_val.type == JSValue::Type::STRING || obj_val.type == JSValue::Type::NUMBER ||
                   obj_val.type == JSValue::Type::BOOLEAN) {
            JSValue proto = primitive_prototype_for(obj_val);
            if (proto.type == JSValue::Type::OBJECT && proto.object_val)
                push(proto.object_val->get_property(prop));
            else
                push(JSValue::undefined());
        } else {
            push(JSValue::undefined());
        }
    }

    void VM::op_get_prop_computed() {
        auto key_val = pop();
        auto obj_val = pop();
        if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val) {
            push(obj_val.object_val->get_property(key_val.to_string()));
        } else if (obj_val.type == JSValue::Type::FUNCTION && obj_val.function_val) {
            auto it = obj_val.function_val->properties.find(key_val.to_string());
            if (it != obj_val.function_val->properties.end()) {
                push(it->second);
            } else if (key_val.to_string() == "prototype") {
                push(obj_val.function_val->prototype_property);
            } else {
                push(JSValue::undefined());
            }
        } else if (obj_val.type == JSValue::Type::STRING || obj_val.type == JSValue::Type::NUMBER ||
                   obj_val.type == JSValue::Type::BOOLEAN) {
            JSValue proto = primitive_prototype_for(obj_val);
            if (proto.type == JSValue::Type::OBJECT && proto.object_val)
                push(proto.object_val->get_property(key_val.to_string()));
            else
                push(JSValue::undefined());
        } else {
            push(JSValue::undefined());
        }
    }

    void VM::op_set_prop(const std::string &prop) {
        auto val = pop();
        auto obj_val = pop();
        if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val) {
            obj_val.object_val->set_property(prop, val);
        } else if (obj_val.type == JSValue::Type::FUNCTION && obj_val.function_val) {
            obj_val.function_val->properties[prop] = val;
        }
        push(val);
    }

    void VM::op_set_prop_computed() {
        auto val = pop();
        auto key_val = pop();
        auto obj_val = pop();
        if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val) {
            obj_val.object_val->set_property(key_val.to_string(), val);
        } else if (obj_val.type == JSValue::Type::FUNCTION && obj_val.function_val) {
            obj_val.function_val->properties[key_val.to_string()] = val;
        }
        push(val);
    }

    void VM::op_delete_prop(const std::string &prop) {
        auto obj_val = pop();
        bool removed = false;
        if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val)
            removed = obj_val.object_val->del_property(prop);
        push(JSValue::boolean(removed));
    }

    void VM::op_delete_prop_computed() {
        auto key_val = pop();
        auto obj_val = pop();
        bool removed = false;
        if (obj_val.type == JSValue::Type::OBJECT && obj_val.object_val)
            removed = obj_val.object_val->del_property(key_val.to_string());
        push(JSValue::boolean(removed));
    }

    void VM::op_typeof() {
        auto v = pop();
        switch (v.type) {
            case JSValue::Type::NUMBER:
                push(JSValue::string("number"));
                break;
            case JSValue::Type::STRING:
                push(JSValue::string("string"));
                break;
            case JSValue::Type::BOOLEAN:
                push(JSValue::string("boolean"));
                break;
            case JSValue::Type::UNDEFINED:
                push(JSValue::string("undefined"));
                break;
            case JSValue::Type::NULL_VAL:
                push(JSValue::string("object"));
                break;
            case JSValue::Type::OBJECT:
                push(JSValue::string("object"));
                break;
            case JSValue::Type::FUNCTION:
                push(JSValue::string("function"));
                break;
            default:
                push(JSValue::string("undefined"));
                break;
        }
    }

    void VM::op_instanceof() {
        auto proto_val = pop();
        auto obj_val = pop();
        bool result = false;
        if (proto_val.type == JSValue::Type::FUNCTION && proto_val.function_val) {
            JSValue proto_prop = proto_val.function_val->prototype_property;
            if (proto_prop.type == JSValue::Type::OBJECT && proto_prop.object_val) {
                result = JSObject::prototype_chain_contains(obj_val, proto_prop);
            }
        } else if (proto_val.type == JSValue::Type::OBJECT && proto_val.object_val) {
            result = JSObject::prototype_chain_contains(obj_val, proto_val);
        }
        push(JSValue::boolean(result));
    }

    void VM::op_negate() {
        auto v = pop();
        push(JSValue::number(-v.to_number()));
    }

    void VM::op_not() {
        auto v = pop();
        push(JSValue::boolean(!v.is_truthy()));
    }

    void VM::op_void() {
        pop();
        push(JSValue::undefined());
    }

    void VM::op_bitwise_not() {
        auto v = pop();
        push(JSValue::number((f64)(~(i32)v.to_number())));
    }

    void VM::op_bitwise_and() {
        auto b = pop();
        auto a = pop();
        push(JSValue::number((f64)((i32)a.to_number() & (i32)b.to_number())));
    }

    void VM::op_bitwise_or() {
        auto b = pop();
        auto a = pop();
        push(JSValue::number((f64)((i32)a.to_number() | (i32)b.to_number())));
    }

    void VM::op_bitwise_xor() {
        auto b = pop();
        auto a = pop();
        push(JSValue::number((f64)((i32)a.to_number() ^ (i32)b.to_number())));
    }

    void VM::op_and() {
        auto b = pop();
        auto a = pop();
        push(a.is_truthy() ? b : a);
    }

    void VM::op_or() {
        auto b = pop();
        auto a = pop();
        push(a.is_truthy() ? a : b);
    }

    void VM::op_template_literal(u32 quasi_count) {
        u32 total = 2 * quasi_count - 1;
        std::vector<JSValue> items;
        for (u32 i = 0; i < total; i++) {
            items.push_back(pop());
        }
        std::string result;
        for (u32 i = 0; i < total; i++) {
            result += items[total - 1 - i].to_string();
        }
        push(JSValue::string(result));
    }

    void VM::op_throw() {
        auto val = pop();
        thrown_value_ = val;
        while (!frames_.empty()) {
            auto &f = frames_.back();
            // J-C3: unwind to the innermost active handler across frames.
            if (!f.handlers.empty()) {
                f.ip = f.handlers.back();
                f.handlers.pop_back();
                push(val);
                thrown_value_ = JSValue::undefined();
                break;
            }
            pop_frame();
        }
        if (frames_.empty())
            thrown_value_ = JSValue::undefined();
    }

    void VM::op_new_object() {
        auto *obj = heap_->alloc_object();
        // J-C2: literals get the canonical Object.prototype. The "Object"
        // global is registered as a plain object whose "prototype" property
        // holds the shared prototype object.
        JSValue ctor = global_->obj.get("Object");
        JSValue proto;
        if (ctor.type == JSValue::Type::OBJECT && ctor.object_val)
            proto = ctor.object_val->get("prototype");
        if (proto.type == JSValue::Type::OBJECT && proto.object_val)
            obj->obj.prototype = proto;
        push(JSValue::object(&obj->obj));
    }

    void VM::op_new_array(u32 count) {
        if (count > stack_.size()) {
            push(JSValue::undefined());
            return;
        }
        auto *arr = heap_->alloc_object();
        arr->obj.is_array = true;
        // J-C2: array literals get Array.prototype so [1,2].push() works.
        JSValue ctor = global_->obj.get("Array");
        if (ctor.type == JSValue::Type::FUNCTION && ctor.function_val &&
            ctor.function_val->prototype_property.type == JSValue::Type::OBJECT)
            arr->obj.prototype = ctor.function_val->prototype_property;
        u32 start = (u32)stack_.size() - count;
        for (u32 i = 0; i < count; i++) {
            arr->obj.array_elements.push_back(stack_[start + i]);
        }
        stack_.resize(start);
        push(JSValue::object(&arr->obj));
    }

    void VM::op_define_prop() {
        auto val = pop();
        auto key = pop();
        if (!stack_.empty() && stack_.back().type == JSValue::Type::OBJECT) {
            stack_.back().object_val->set(key.to_string(), val);
        }
    }

    void VM::op_call(u32 argc) {
        if (stack_.size() < argc + 1) {
            push(JSValue::undefined());
            return;
        }
        auto callee = stack_[stack_.size() - 1 - argc];
        if (callee.type == JSValue::Type::FUNCTION) {
            JSFunction *fn = callee.function_val;
            if (fn->native_fn) {
                std::vector<JSValue> args;
                args.push_back(JSValue::object(&global_->obj));
                for (u32 i = 0; i < argc; i++) {
                    args.push_back(stack_[stack_.size() - argc + i]);
                }
                stack_.resize(stack_.size() - argc - 1);
                NativeCallScope gc_guard(*this);
                push(fn->native_fn(args, fn->native_context));
            } else if (fn->bytecode) {
                auto *new_frame = push_call_frame(fn, argc);
                if (new_frame)
                    new_frame->this_value = JSValue::object(&global_->obj);
            } else {
                stack_.resize(stack_.size() - argc - 1);
                push(JSValue::undefined());
            }
        } else {
            stack_.resize(stack_.size() - argc - 1);
            push(JSValue::undefined());
        }
    }

    void VM::op_call_method(const Instruction::CallMethodInfo &info) {
        u32 argc = info.argc;
        bool is_computed = info.method_name.empty();
        u32 needed = argc + 1 + (is_computed ? 1 : 0);
        if (stack_.size() < needed) {
            push(JSValue::undefined());
            return;
        }
        JSValue callee;
        JSValue receiver_val;
        auto fn_property = [&](const JSValue &recv, const std::string &name) -> JSValue {
            if (recv.type == JSValue::Type::OBJECT && recv.object_val)
                return recv.object_val->get_property(name);
            if (recv.type == JSValue::Type::FUNCTION && recv.function_val) {
                auto it = recv.function_val->properties.find(name);
                if (it != recv.function_val->properties.end())
                    return it->second;
            }
            // J-C2: primitive receivers resolve methods via their wrapper
            // prototype ("abc".trim(), (5).toFixed(2), true.toString()).
            JSValue proto = primitive_prototype_for(recv);
            if (proto.type == JSValue::Type::OBJECT && proto.object_val)
                return proto.object_val->get_property(name);
            return JSValue::undefined();
        };
        auto resolve_method = [&]() {
            u32 obj_idx = stack_.size() - 1 - argc;
            if (is_computed)
                obj_idx--;
            JSValue obj_val = stack_[obj_idx];
            receiver_val = obj_val;
            if (is_computed) {
                JSValue key_val = stack_[obj_idx + 1];
                callee = fn_property(obj_val, key_val.to_string());
                stack_.erase(stack_.begin() + obj_idx + 1);
            } else {
                callee = fn_property(obj_val, info.method_name);
            }
            if (obj_idx < stack_.size()) {
                stack_[obj_idx] = callee;
            }
        };
        resolve_method();
        if (callee.type == JSValue::Type::FUNCTION) {
            JSFunction *fn = callee.function_val;
            if (fn->native_fn) {
                std::vector<JSValue> args;
                args.push_back(receiver_val);
                for (u32 i = 0; i < argc; i++) {
                    args.push_back(stack_[stack_.size() - argc + i]);
                }
                stack_.resize(stack_.size() - argc - 1);
                NativeCallScope gc_guard(*this);
                push(fn->native_fn(args, fn->native_context));
            } else if (fn->bytecode) {
                auto *new_frame = push_call_frame(fn, argc);
                if (new_frame)
                    new_frame->this_value = receiver_val;
            } else {
                stack_.resize(stack_.size() - argc - 1);
                push(JSValue::undefined());
            }
        } else {
            stack_.resize(stack_.size() - argc - 1);
            push(JSValue::undefined());
        }
    }

    void VM::op_new(u32 argc) {
        if (stack_.size() < argc + 1) {
            push(JSValue::undefined());
            return;
        }
        auto ctor_val = stack_[stack_.size() - 1 - argc];
        if (ctor_val.type == JSValue::Type::FUNCTION) {
            JSFunction *ctor = ctor_val.function_val;
            auto *obj_gc = heap_->alloc_object();
            JSObject *new_obj = &obj_gc->obj;
            JSValue proto = ctor->prototype_property;
            if (proto.type == JSValue::Type::OBJECT) {
                new_obj->prototype = proto;
            }
            if (ctor->native_fn) {
                std::vector<JSValue> args;
                args.push_back(JSValue::object(new_obj));
                for (u32 i = 0; i < argc; i++) {
                    args.push_back(stack_[stack_.size() - argc + i]);
                }
                stack_.resize(stack_.size() - argc - 1);
                NativeCallScope gc_guard(*this);
                JSValue result = ctor->native_fn(args, ctor->native_context);
                if (result.type == JSValue::Type::OBJECT || result.type == JSValue::Type::FUNCTION) {
                    push(result);
                } else {
                    push(JSValue::object(new_obj));
                }
            } else if (ctor->bytecode) {
                auto *new_frame = push_call_frame(ctor, argc);
                if (new_frame) {
                    new_frame->this_value = JSValue::object(new_obj);
                    new_frame->new_object = JSValue::object(new_obj);
                }
            } else {
                stack_.resize(stack_.size() - argc - 1);
                push(JSValue::object(new_obj));
            }
        } else {
            stack_.resize(stack_.size() - argc - 1);
            push(JSValue::undefined());
        }
    }

    void VM::op_push_function(u32 idx) {
        auto &frame = frames_.back();
        auto *func = frame.function;
        auto *child_bc = func->child_functions[idx].get();
        auto *gc_fn = heap_->alloc_function();
        gc_fn->fn.bytecode = child_bc;
        gc_fn->fn.name = child_bc->name;
        push(JSValue::function(&gc_fn->fn));
    }

    // ---------------------------------------------------------------------------
    // JIT helpers (extern "C")
    // ---------------------------------------------------------------------------

    extern "C" {

    void jit_push_number(VM *vm, u64 val_bits) {
        f64 val;
        std::memcpy(&val, &val_bits, sizeof(val));
        vm->push(JSValue::number(val));
    }

    void jit_push_undefined(VM *vm) {
        vm->push(JSValue::undefined());
    }

    void jit_pop(VM *vm) {
        vm->pop();
    }

    void jit_call_add(VM *vm) {
        auto b = vm->pop();
        auto a = vm->pop();
        vm->push(vm->add(a, b));
    }

    void jit_call_sub(VM *vm) {
        auto b = vm->pop();
        auto a = vm->pop();
        vm->push(JSValue::number(a.to_number() - b.to_number()));
    }

    void jit_call_mul(VM *vm) {
        auto b = vm->pop();
        auto a = vm->pop();
        vm->push(JSValue::number(a.to_number() * b.to_number()));
    }

    void jit_call_div(VM *vm) {
        auto b = vm->pop();
        auto a = vm->pop();
        f64 denom = b.to_number();
        if (denom == 0) {
            f64 num = a.to_number();
            vm->push(JSValue::number(num == 0 ? NAN : (num > 0 ? INFINITY : -INFINITY)));
        } else {
            vm->push(JSValue::number(a.to_number() / denom));
        }
    }

    void jit_call_mod(VM *vm) {
        auto b = vm->pop();
        auto a = vm->pop();
        vm->push(JSValue::number(std::fmod(a.to_number(), b.to_number())));
    }

    u32 jit_pop_and_is_truthy(VM *vm) {
        auto v = vm->pop();
        return v.is_truthy() ? 1 : 0;
    }
    }

}  // namespace browser::js
