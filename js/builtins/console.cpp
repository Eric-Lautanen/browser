#include "builtins.hpp"
#include <windows.h>
#include <sstream>
#include <chrono>
#include <unordered_map>
#include <iostream>

namespace browser::js::builtins {

static std::unordered_map<std::string, std::chrono::steady_clock::time_point> timers;
static std::unordered_map<std::string, u32> counters;

static void debug_out(const std::string& msg) {
    OutputDebugStringA(msg.c_str());
    std::cout << msg;
}

static JSValue console_log(const std::vector<JSValue>& args, void*) {
    std::ostringstream oss;
    for (u32 i = 1; i < args.size(); i++) {
        if (i > 1) oss << " ";
        oss << args[i].to_string();
    }
    debug_out(oss.str() + "\n");
    return JSValue::undefined();
}

static JSValue console_warn(const std::vector<JSValue>& args, void*) {
    std::ostringstream oss;
    oss << "WARN: ";
    for (u32 i = 1; i < args.size(); i++) {
        if (i > 1) oss << " ";
        oss << args[i].to_string();
    }
    debug_out(oss.str() + "\n");
    return JSValue::undefined();
}

static JSValue console_error(const std::vector<JSValue>& args, void*) {
    std::ostringstream oss;
    oss << "ERROR: ";
    for (u32 i = 1; i < args.size(); i++) {
        if (i > 1) oss << " ";
        oss << args[i].to_string();
    }
    debug_out(oss.str() + "\n");
    return JSValue::undefined();
}

static JSValue console_info(const std::vector<JSValue>& args, void*) {
    return console_log(args, nullptr);
}

static JSValue console_debug(const std::vector<JSValue>& args, void*) {
    return console_log(args, nullptr);
}

static JSValue console_group(const std::vector<JSValue>& args, void*) {
    std::ostringstream oss;
    oss << "GROUP: ";
    for (u32 i = 1; i < args.size(); i++) {
        if (i > 1) oss << " ";
        oss << args[i].to_string();
    }
    debug_out(oss.str() + "\n");
    return JSValue::undefined();
}

static JSValue console_group_end(const std::vector<JSValue>&, void*) {
    debug_out("GROUP_END\n");
    return JSValue::undefined();
}

static JSValue console_time(const std::vector<JSValue>& args, void*) {
    std::string label = args.size() > 1 ? args[1].to_string() : "default";
    timers[label] = std::chrono::steady_clock::now();
    return JSValue::undefined();
}

static JSValue console_time_end(const std::vector<JSValue>& args, void*) {
    std::string label = args.size() > 1 ? args[1].to_string() : "default";
    auto it = timers.find(label);
    if (it != timers.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - it->second).count();
        debug_out(label + ": " + std::to_string(elapsed) + "ms\n");
        timers.erase(it);
    }
    return JSValue::undefined();
}

static JSValue console_count(const std::vector<JSValue>& args, void*) {
    std::string label = args.size() > 1 ? args[1].to_string() : "default";
    u32& c = counters[label];
    c++;
    debug_out(label + ": " + std::to_string(c) + "\n");
    return JSValue::undefined();
}

static JSValue console_assert_fn(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::undefined();
    if (!args[1].is_truthy()) {
        std::ostringstream oss;
        oss << "ASSERTION FAILED: ";
        for (u32 i = 2; i < args.size(); i++) {
            if (i > 2) oss << " ";
            oss << args[i].to_string();
        }
        debug_out(oss.str() + "\n");
    }
    return JSValue::undefined();
}

static JSValue console_clear(const std::vector<JSValue>&, void*) {
    debug_out("[clear]\n");
    return JSValue::undefined();
}

void register_console_builtins(VM* vm) {
    auto* console_obj = vm->heap()->alloc_object();
    console_obj->obj.set("log", JSValue::function(make_fn(vm, console_log)));
    console_obj->obj.set("warn", JSValue::function(make_fn(vm, console_warn)));
    console_obj->obj.set("error", JSValue::function(make_fn(vm, console_error)));
    console_obj->obj.set("info", JSValue::function(make_fn(vm, console_info)));
    console_obj->obj.set("debug", JSValue::function(make_fn(vm, console_debug)));
    console_obj->obj.set("group", JSValue::function(make_fn(vm, console_group)));
    console_obj->obj.set("groupEnd", JSValue::function(make_fn(vm, console_group_end)));
    console_obj->obj.set("time", JSValue::function(make_fn(vm, console_time)));
    console_obj->obj.set("timeEnd", JSValue::function(make_fn(vm, console_time_end)));
    console_obj->obj.set("count", JSValue::function(make_fn(vm, console_count)));
    console_obj->obj.set("assert", JSValue::function(make_fn(vm, console_assert_fn)));
    console_obj->obj.set("clear", JSValue::function(make_fn(vm, console_clear)));
    vm->global_object()->set("console", JSValue::object(&console_obj->obj));
}

} // namespace browser::js::builtins
