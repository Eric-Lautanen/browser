#include "builtins.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>

namespace browser::js::builtins {

struct TimerEntry {
    u32 id;
    f64 expiry_ms;
    f64 interval;
    JSValue callback;
    bool repeat;
    bool cancelled;
};

// J-M2: per-VM timer/microtask state whose JSValues are visible to the GC via
// a root provider registered in register_timer_builtins(). Entries live in
// heap-allocated cells so the callback JSValues have stable addresses.
struct TimerState {
    std::vector<std::unique_ptr<TimerEntry>> queue;
    std::vector<JSValue> microtasks;
    u32 next_id = 1;
    f64 start_ms = 0;
};

static std::unordered_map<VM *, TimerState> &timer_states() {
    static std::unordered_map<VM *, TimerState> states;
    return states;
}

static TimerState &state_for(VM *vm) {
    return timer_states()[vm];
}

static f64 now_ms_steady() {
    return static_cast<f64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Called from BrowserWindow event loop
void fire_expired_timers(VM *vm) {
    auto &st = state_for(vm);
    if (st.start_ms == 0)
        st.start_ms = now_ms_steady();
    f64 now = now_ms_steady() - st.start_ms;
    // Collect IDs of expired timers (index-based to avoid iterator invalidation)
    std::vector<u32> to_fire;
    for (auto &t : st.queue) {
        if (!t->cancelled && now >= t->expiry_ms) {
            to_fire.push_back(t->id);
        }
    }
    if (!to_fire.empty()) {
        // J-M5: callbacks may re-enter bytecode; keep GC suspended throughout.
        VM::NativeCallScope gc_guard(*vm);
        for (u32 id : to_fire) {
            for (auto &t : st.queue) {
                if (t->id == id && !t->cancelled) {
                    if (t->callback.type == JSValue::Type::FUNCTION) {
                        vm->invoke(t->callback, {});
                    }
                    if (t->repeat) {
                        t->expiry_ms = now + t->interval;
                    } else {
                        t->cancelled = true;
                    }
                    break;
                }
            }
        }
    }
    st.queue.erase(std::remove_if(st.queue.begin(),
                                  st.queue.end(),
                                  [](const std::unique_ptr<TimerEntry> &t) { return t->cancelled && !t->repeat; }),
                   st.queue.end());
}

void drain_microtask_queue(VM *vm) {
    auto &st = state_for(vm);
    if (st.microtasks.empty())
        return;
    auto queue = std::move(st.microtasks);
    st.microtasks.clear();
    VM::NativeCallScope gc_guard(*vm);
    for (auto& task : queue) {
        if (task.type == JSValue::Type::FUNCTION && task.function_val) {
            vm->invoke(task, {});
        }
    }
}

void cancel_timer(u32 id) {
    // Timers are keyed per VM; cancellation by id applies to whichever state
    // holds it. clear* builtins run on their own VM, so this is sufficient.
    for (auto &[vm, st] : timer_states()) {
        for (auto &t : st.queue) {
            if (t->id == id) {
                t->cancelled = true;
                return;
            }
        }
    }
}

static JSValue timer_set_timeout(const std::vector<JSValue> &args, void *context) {
    if (args.size() < 2) return JSValue::number(0);
    JSValue callback = args[1];
    f64 delay = get_number_arg(args, 2, 0);
    auto &st = state_for(static_cast<VM *>(context));
    if (st.start_ms == 0)
        st.start_ms = now_ms_steady();
    f64 now = now_ms_steady() - st.start_ms;
    u32 id = st.next_id++;
    st.queue.push_back(std::make_unique<TimerEntry>(TimerEntry{id, now + delay, 0, callback, false, false}));
    return JSValue::number(static_cast<f64>(id));
}

static JSValue timer_set_interval(const std::vector<JSValue> &args, void *context) {
    if (args.size() < 2) return JSValue::number(0);
    JSValue callback = args[1];
    f64 delay = get_number_arg(args, 2, 0);
    auto &st = state_for(static_cast<VM *>(context));
    if (st.start_ms == 0)
        st.start_ms = now_ms_steady();
    f64 now = now_ms_steady() - st.start_ms;
    u32 id = st.next_id++;
    st.queue.push_back(std::make_unique<TimerEntry>(TimerEntry{id, now + delay, delay, callback, true, false}));
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

static JSValue timer_request_animation_frame(const std::vector<JSValue> &args, void *context) {
    if (args.size() < 2) return JSValue::number(0);
    JSValue callback = args[1];
    auto &st = state_for(static_cast<VM *>(context));
    if (st.start_ms == 0)
        st.start_ms = now_ms_steady();
    u32 id = st.next_id++;
    st.queue.push_back(std::make_unique<TimerEntry>(TimerEntry{id, 0, 0, callback, false, false}));
    return JSValue::number(static_cast<f64>(id));
}

static JSValue timer_cancel_animation_frame(const std::vector<JSValue>& args, void*) {
    return timer_clear_timeout(args, nullptr);
}

static JSValue timer_queue_microtask(const std::vector<JSValue> &args, void *context) {
    if (args.size() < 2) return JSValue::undefined();
    state_for(static_cast<VM *>(context)).microtasks.push_back(args[1]);
    return JSValue::undefined();
}

void register_timer_builtins(VM* vm) {
    // J-M2: make pending timer/microtask callbacks visible to the collector.
    vm->add_gc_root_provider([vm]() -> std::vector<JSValue *> {
        std::vector<JSValue *> roots;
        auto it = timer_states().find(vm);
        if (it == timer_states().end())
            return roots;
        for (auto &e : it->second.queue) roots.push_back(&e->callback);
        for (auto &v : it->second.microtasks) roots.push_back(&v);
        return roots;
    });
    vm->global_object()->set("setTimeout", JSValue::function(make_fn(vm, timer_set_timeout, false, vm)));
    vm->global_object()->set("setInterval", JSValue::function(make_fn(vm, timer_set_interval, false, vm)));
    vm->global_object()->set("clearTimeout", JSValue::function(make_fn(vm, timer_clear_timeout)));
    vm->global_object()->set("clearInterval", JSValue::function(make_fn(vm, timer_clear_interval)));
    vm->global_object()->set("requestAnimationFrame",
                             JSValue::function(make_fn(vm, timer_request_animation_frame, false, vm)));
    vm->global_object()->set("cancelAnimationFrame", JSValue::function(make_fn(vm, timer_cancel_animation_frame)));
    vm->global_object()->set("queueMicrotask", JSValue::function(make_fn(vm, timer_queue_microtask, false, vm)));
}

} // namespace browser::js::builtins
