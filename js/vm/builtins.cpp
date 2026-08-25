#include "../builtins/builtins.hpp"

#include "gc.hpp"
#include "vm.hpp"

#include <cstdlib>
#include <iostream>

namespace browser::js {

    void VM::register_builtins() {
        auto *console_obj = heap_->alloc_object();
        console_obj->obj.set(
            "log", JSValue::function(create_native_fn([](const std::vector<JSValue> &args, void *) -> JSValue {
                for (u32 i = 1; i < args.size(); i++) {
                    if (i > 1)
                        std::cout << " ";
                    std::cout << args[i].to_string();
                }
                std::cout << std::endl;
                return JSValue::undefined();
            })));
        global_->obj.set("console", JSValue::object(&console_obj->obj));

        global_->obj.set("parseInt",
                         JSValue::function(create_native_fn([](const std::vector<JSValue> &args, void *) -> JSValue {
                             if (args.size() < 2)
                                 return JSValue::number(NAN);
                             return JSValue::number((f64)std::strtol(args[1].to_string().c_str(), nullptr, 10));
                         })));

        global_->obj.set("parseFloat",
                         JSValue::function(create_native_fn([](const std::vector<JSValue> &args, void *) -> JSValue {
                             if (args.size() < 2)
                                 return JSValue::number(NAN);
                             return JSValue::number(std::strtod(args[1].to_string().c_str(), nullptr));
                         })));

        // Percent-encoding per RFC 3986 (application/x-www-form-urlencoded rules
        // for encodeURIComponent: unreserved = A-Z a-z 0-9 - _ . ! ~ * ' ( )).
        global_->obj.set("encodeURIComponent",
                         JSValue::function(create_native_fn([](const std::vector<JSValue> &args, void *) -> JSValue {
                             if (args.size() < 2)
                                 return JSValue::string("");
                             std::string s = args[1].to_string();
                             static const char *hex = "0123456789ABCDEF";
                             std::string out;
                             out.reserve(s.size());
                             for (unsigned char c : s) {
                                 if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                     c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' ||
                                     c == '\'' || c == '(' || c == ')') {
                                     out += (char)c;
                                 } else {
                                     out += '%';
                                     out += hex[c >> 4];
                                     out += hex[c & 0xF];
                                 }
                             }
                             return JSValue::string(out);
                         })));
        global_->obj.set("decodeURIComponent",
                         JSValue::function(create_native_fn([](const std::vector<JSValue> &args, void *) -> JSValue {
                             if (args.size() < 2)
                                 return JSValue::string("");
                             std::string s = args[1].to_string();
                             std::string out;
                             out.reserve(s.size());
                             auto hex_val = [](char c) -> int {
                                 if (c >= '0' && c <= '9')
                                     return c - '0';
                                 if (c >= 'a' && c <= 'f')
                                     return c - 'a' + 10;
                                 if (c >= 'A' && c <= 'F')
                                     return c - 'A' + 10;
                                 return -1;
                             };
                             for (size_t i = 0; i < s.size(); i++) {
                                 char c = s[i];
                                 if (c == '%' && i + 2 < s.size()) {
                                     int hi = hex_val(s[i + 1]);
                                     int lo = hex_val(s[i + 2]);
                                     if (hi >= 0 && lo >= 0) {
                                         out += (char)((hi << 4) | lo);
                                         i += 2;
                                         continue;
                                     }
                                 }
                                 if (c == '+')
                                     c = ' ';
                                 out += c;
                             }
                             // Decode UTF-8 bytes into a UTF-8 string is identity here:
                             // the byte sequence already forms valid UTF-8 when the
                             // input was produced by encodeURIComponent.
                             return JSValue::string(out);
                         })));

        global_->obj.set("Array",
                         JSValue::function(create_native_fn(
                             [](const std::vector<JSValue> &args, void *context) -> JSValue {
                                 auto *vm = static_cast<VM *>(context);
                                 auto *arr = vm->heap()->alloc_object();
                                 arr->obj.is_array = true;
                                 for (u32 i = 1; i < args.size(); i++) {
                                     arr->obj.array_elements.push_back(args[i]);
                                 }
                                 return JSValue::object(&arr->obj);
                             },
                             true,
                             this)));

        global_->obj.set("NaN", JSValue::number(NAN));
        global_->obj.set("undefined", JSValue::undefined());

        // Conversion globals used pervasively by real scripts.
        global_->obj.set("String",
                         JSValue::function(create_native_fn([](const std::vector<JSValue> &args, void *) -> JSValue {
                             return args.size() > 1 ? JSValue::string(args[1].to_string()) : JSValue::string("");
                         })));
        global_->obj.set("Number",
                         JSValue::function(create_native_fn([](const std::vector<JSValue> &args, void *) -> JSValue {
                             return args.size() > 1 ? JSValue::number(args[1].to_number()) : JSValue::number(0);
                         })));
        global_->obj.set("Boolean",
                         JSValue::function(create_native_fn([](const std::vector<JSValue> &args, void *) -> JSValue {
                             return args.size() > 1 ? JSValue::boolean(args[1].is_truthy()) : JSValue::boolean(false);
                         })));

        // Register builtin groups
        builtins::register_string_prototype(this);
        builtins::register_array_prototype(this);
        builtins::register_object_builtins(this);
        builtins::register_math_builtins(this);
        builtins::register_number_builtins(this);
        builtins::register_symbol_builtins(this);
        builtins::register_json_builtins(this);
        builtins::register_date_builtins(this);
        builtins::register_regexp_builtins(this);
        builtins::register_error_builtins(this);
        builtins::register_console_builtins(this);
        builtins::register_timer_builtins(this);
        builtins::register_promise_builtins(this);
        builtins::register_performance_builtins(this);
    }

    JSFunction *VM::create_native_fn(JSFunction::NativeFn fn, bool is_constructor, void *context) {
        auto *f = heap_->alloc_function();
        f->fn.native_fn = fn;
        f->fn.is_constructor = is_constructor;
        f->fn.native_context = context;
        return &f->fn;
    }

}  // namespace browser::js
