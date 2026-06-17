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
