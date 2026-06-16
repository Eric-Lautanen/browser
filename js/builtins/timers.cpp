#include "builtins.hpp"
#include <chrono>
#include <vector>
#include <algorithm>
#include <queue>

namespace browser::js::builtins {

struct TimerEntry {
    u32 id;
    f64 expiry_ms;
    f64 interval;
    JSValue callback;
    bool repeat;
    bool cancelled;
};

static std::vector<TimerEntry> timer_queue;
static u32 next_timer_id = 1;
static f64 timer_start_ms = 0;
static std::vector<JSValue> microtask_queue;

static f64 now_ms_steady() {
    return static_cast<f64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Called from BrowserWindow event loop
void fire_expired_timers(VM*) {
    if (timer_start_ms == 0) timer_start_ms = now_ms_steady();
    f64 now = now_ms_steady() - timer_start_ms;
    // Collect IDs of expired timers (index-based to avoid iterator invalidation)
    std::vector<u32> to_fire;
    for (auto& t : timer_queue) {
        if (!t.cancelled && now >= t.expiry_ms) {
            to_fire.push_back(t.id);
        }
    }
    for (u32 id : to_fire) {
        for (auto& t : timer_queue) {
            if (t.id == id && !t.cancelled) {
                if (t.callback.type == JSValue::Type::FUNCTION) {
                    auto* fn = t.callback.function_val;
                    if (fn && fn->native_fn) {
                        fn->native_fn({}, fn->native_context);
                    }
                }
                if (t.repeat) {
                    t.expiry_ms = now + t.interval;
                } else {
                    t.cancelled = true;
                }
                break;
            }
        }
    }
    timer_queue.erase(std::remove_if(timer_queue.begin(), timer_queue.end(),
        [](const TimerEntry& t) { return t.cancelled && !t.repeat; }), timer_queue.end());
}

void drain_microtask_queue(VM*) {
    auto queue = std::move(microtask_queue);
    for (auto& task : queue) {
        if (task.type == JSValue::Type::FUNCTION && task.function_val && task.function_val->native_fn) {
            task.function_val->native_fn({}, task.function_val->native_context);
        }
    }
}

void cancel_timer(u32 id) {
    for (auto& t : timer_queue) {
        if (t.id == id) {
            t.cancelled = true;
            break;
        }
    }
}

static JSValue timer_set_timeout(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::number(0);
    JSValue callback = args[1];
    f64 delay = get_number_arg(args, 2, 0);
    if (timer_start_ms == 0) timer_start_ms = now_ms_steady();
    f64 now = now_ms_steady() - timer_start_ms;
    u32 id = next_timer_id++;
    timer_queue.push_back({id, now + delay, 0, callback, false, false});
    return JSValue::number(static_cast<f64>(id));
}

static JSValue timer_set_interval(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::number(0);
    JSValue callback = args[1];
    f64 delay = get_number_arg(args, 2, 0);
    if (timer_start_ms == 0) timer_start_ms = now_ms_steady();
    f64 now = now_ms_steady() - timer_start_ms;
    u32 id = next_timer_id++;
    timer_queue.push_back({id, now + delay, delay, callback, true, false});
    return JSValue::number(static_cast<f64>(id));
}

static JSValue timer_clear_timeout(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::undefined();
    u32 id = static_cast<u32>(args[1].to_number());
    cancel_timer(id);
    return JSValue::undefined();
}

static JSValue timer_clear_interval(const std::vector<JSValue>& args, void*) {
    return timer_clear_timeout(args, nullptr);
}

static JSValue timer_request_animation_frame(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::number(0);
    JSValue callback = args[1];
    if (timer_start_ms == 0) timer_start_ms = now_ms_steady();
    u32 id = next_timer_id++;
    timer_queue.push_back({id, 0, 0, callback, false, false});
    return JSValue::number(static_cast<f64>(id));
}

static JSValue timer_cancel_animation_frame(const std::vector<JSValue>& args, void*) {
    return timer_clear_timeout(args, nullptr);
}

static JSValue timer_queue_microtask(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::undefined();
    microtask_queue.push_back(args[1]);
    return JSValue::undefined();
}

void register_timer_builtins(VM* vm) {
    vm->global_object()->set("setTimeout", JSValue::function(make_fn(vm, timer_set_timeout)));
    vm->global_object()->set("setInterval", JSValue::function(make_fn(vm, timer_set_interval)));
    vm->global_object()->set("clearTimeout", JSValue::function(make_fn(vm, timer_clear_timeout)));
    vm->global_object()->set("clearInterval", JSValue::function(make_fn(vm, timer_clear_interval)));
    vm->global_object()->set("requestAnimationFrame", JSValue::function(make_fn(vm, timer_request_animation_frame)));
    vm->global_object()->set("cancelAnimationFrame", JSValue::function(make_fn(vm, timer_cancel_animation_frame)));
    vm->global_object()->set("queueMicrotask", JSValue::function(make_fn(vm, timer_queue_microtask)));
}

} // namespace browser::js::builtins
