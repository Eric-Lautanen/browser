#include "builtins.hpp"

namespace browser::js::builtins {

static JSValue math_abs(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::abs(get_number_arg(args, 1, NAN)));
}
static JSValue math_ceil(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::ceil(get_number_arg(args, 1, NAN)));
}
static JSValue math_floor(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::floor(get_number_arg(args, 1, NAN)));
}
static JSValue math_round(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::round(get_number_arg(args, 1, NAN)));
}
static JSValue math_max(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::number(-INFINITY);
    f64 m = args[1].to_number();
    for (u32 i = 2; i < args.size(); i++) m = std::max(m, args[i].to_number());
    return JSValue::number(m);
}
static JSValue math_min(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::number(INFINITY);
    f64 m = args[1].to_number();
    for (u32 i = 2; i < args.size(); i++) m = std::min(m, args[i].to_number());
    return JSValue::number(m);
}
static JSValue math_pow(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::pow(get_number_arg(args, 1, NAN), get_number_arg(args, 2, NAN)));
}
static JSValue math_sqrt(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::sqrt(get_number_arg(args, 1, NAN)));
}
static JSValue math_random(const std::vector<JSValue>&, void*) {
    return JSValue::number(static_cast<f64>(std::rand()) / RAND_MAX);
}
static JSValue math_sin(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::sin(get_number_arg(args, 1, NAN)));
}
static JSValue math_cos(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::cos(get_number_arg(args, 1, NAN)));
}
static JSValue math_tan(const std::vector<JSValue>& args, void*) {
    return JSValue::number(std::tan(get_number_arg(args, 1, NAN)));
}

void register_math_builtins(VM* vm) {
    auto* math_obj = vm->heap()->alloc_object();
    math_obj->obj.set("abs", JSValue::function(make_fn(vm, math_abs)));
    math_obj->obj.set("ceil", JSValue::function(make_fn(vm, math_ceil)));
    math_obj->obj.set("floor", JSValue::function(make_fn(vm, math_floor)));
    math_obj->obj.set("round", JSValue::function(make_fn(vm, math_round)));
    math_obj->obj.set("max", JSValue::function(make_fn(vm, math_max)));
    math_obj->obj.set("min", JSValue::function(make_fn(vm, math_min)));
    math_obj->obj.set("pow", JSValue::function(make_fn(vm, math_pow)));
    math_obj->obj.set("sqrt", JSValue::function(make_fn(vm, math_sqrt)));
    math_obj->obj.set("random", JSValue::function(make_fn(vm, math_random)));
    math_obj->obj.set("sin", JSValue::function(make_fn(vm, math_sin)));
    math_obj->obj.set("cos", JSValue::function(make_fn(vm, math_cos)));
    math_obj->obj.set("tan", JSValue::function(make_fn(vm, math_tan)));
    math_obj->obj.set("PI", JSValue::number(3.141592653589793));
    math_obj->obj.set("E", JSValue::number(2.718281828459045));
    vm->global_object()->set("Math", JSValue::object(&math_obj->obj));
}

} // namespace browser::js::builtins
