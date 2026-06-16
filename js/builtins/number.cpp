#include "builtins.hpp"
#include <sstream>
#include <iomanip>

namespace browser::js::builtins {

static JSValue num_to_fixed(const std::vector<JSValue>& args, void*) {
    f64 n = args[0].to_number();
    i32 digits = get_int_arg(args, 1, 0);
    if (digits < 0 || digits > 100) digits = 0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(digits) << n;
    return JSValue::string(oss.str());
}

static JSValue num_to_exponential(const std::vector<JSValue>& args, void*) {
    f64 n = args[0].to_number();
    i32 digits = get_int_arg(args, 1, -1);
    std::ostringstream oss;
    if (digits >= 0) oss << std::scientific << std::setprecision(digits) << n;
    else oss << std::scientific << n;
    return JSValue::string(oss.str());
}

static JSValue num_to_precision(const std::vector<JSValue>& args, void*) {
    f64 n = args[0].to_number();
    i32 prec = get_int_arg(args, 1, -1);
    if (prec < 0) return JSValue::string(args[0].to_string());
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << n;
    return JSValue::string(oss.str());
}

static JSValue num_to_string(const std::vector<JSValue>& args, void*) {
    f64 n = args[0].to_number();
    i32 radix = get_int_arg(args, 1, 10);
    if (radix == 10) return JSValue::string(args[0].to_string());
    if (radix < 2 || radix > 36) return JSValue::string(args[0].to_string());
    i64 val = static_cast<i64>(n);
    if (val < 0) { val = -val; }
    std::string result;
    const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    do {
        result = digits[val % radix] + result;
        val /= radix;
    } while (val > 0);
    if (n < 0) result = "-" + result;
    return JSValue::string(result);
}

static JSValue num_parse_int(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::number(NAN);
    i32 radix = get_int_arg(args, 2, 10);
    return JSValue::number(static_cast<f64>(std::strtoll(args[1].to_string().c_str(), nullptr, radix)));
}

static JSValue num_parse_float(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::number(NAN);
    return JSValue::number(std::strtod(args[1].to_string().c_str(), nullptr));
}

static JSValue num_is_nan(const std::vector<JSValue>& args, void*) {
    return JSValue::boolean(args.size() > 1 && std::isnan(args[1].to_number()));
}

static JSValue num_is_finite(const std::vector<JSValue>& args, void*) {
    return JSValue::boolean(args.size() > 1 && std::isfinite(args[1].to_number()));
}

static JSValue num_is_integer(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::boolean(false);
    f64 n = args[1].to_number();
    return JSValue::boolean(std::isfinite(n) && std::floor(n) == n);
}

static JSValue num_is_safe_integer(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::boolean(false);
    f64 n = args[1].to_number();
    return JSValue::boolean(std::isfinite(n) && std::floor(n) == n && std::abs(n) <= 9007199254740991.0);
}

void register_number_builtins(VM* vm) {
    auto* num_proto = vm->heap()->alloc_object();
    set_prototype_method(&num_proto->obj, "toFixed", make_fn(vm, num_to_fixed));
    set_prototype_method(&num_proto->obj, "toExponential", make_fn(vm, num_to_exponential));
    set_prototype_method(&num_proto->obj, "toPrecision", make_fn(vm, num_to_precision));
    set_prototype_method(&num_proto->obj, "toString", make_fn(vm, num_to_string));
    auto* num_ctor = vm->heap()->alloc_object();
    num_ctor->obj.set("prototype", JSValue::object(&num_proto->obj));
    num_ctor->obj.set("parseInt", JSValue::function(make_fn(vm, num_parse_int)));
    num_ctor->obj.set("parseFloat", JSValue::function(make_fn(vm, num_parse_float)));
    num_ctor->obj.set("isNaN", JSValue::function(make_fn(vm, num_is_nan)));
    num_ctor->obj.set("isFinite", JSValue::function(make_fn(vm, num_is_finite)));
    num_ctor->obj.set("isInteger", JSValue::function(make_fn(vm, num_is_integer)));
    num_ctor->obj.set("isSafeInteger", JSValue::function(make_fn(vm, num_is_safe_integer)));
    vm->global_object()->set("Number", JSValue::object(&num_ctor->obj));
}

} // namespace browser::js::builtins
