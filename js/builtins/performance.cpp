#include "builtins.hpp"
#include <windows.h>

namespace browser::js::builtins {

static f64 get_hires_time() {
    LARGE_INTEGER freq, counter;
    if (QueryPerformanceFrequency(&freq) && QueryPerformanceCounter(&counter)) {
        return static_cast<f64>(counter.QuadPart) / static_cast<f64>(freq.QuadPart) * 1000.0;
    }
    return 0.0;
}

static JSValue perf_now(const std::vector<JSValue>&, void*) {
    return JSValue::number(get_hires_time());
}

void register_performance_builtins(VM* vm) {
    auto* perf_obj = vm->heap()->alloc_object();
    perf_obj->obj.set("now", JSValue::function(make_fn(vm, perf_now)));
    vm->global_object()->set("performance", JSValue::object(&perf_obj->obj));
}

} // namespace browser::js::builtins
