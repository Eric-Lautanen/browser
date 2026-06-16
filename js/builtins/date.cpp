#include "builtins.hpp"
#include <chrono>
#include <ctime>

namespace browser::js::builtins {

static f64 now_ms() {
    return static_cast<f64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

static std::tm to_tm(f64 ms) {
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm;
    gmtime_s(&tm, &t);
    return tm;
}

static JSValue date_get_time(const std::vector<JSValue>&, void*) {
    return JSValue::number(now_ms());
}
static JSValue date_get_full_year(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    return JSValue::number(static_cast<f64>(tm.tm_year + 1900));
}
static JSValue date_get_month(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    return JSValue::number(static_cast<f64>(tm.tm_mon));
}
static JSValue date_get_date(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    return JSValue::number(static_cast<f64>(tm.tm_mday));
}
static JSValue date_get_day(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    return JSValue::number(static_cast<f64>(tm.tm_wday));
}
static JSValue date_get_hours(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    return JSValue::number(static_cast<f64>(tm.tm_hour));
}
static JSValue date_get_minutes(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    return JSValue::number(static_cast<f64>(tm.tm_min));
}
static JSValue date_get_seconds(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    return JSValue::number(static_cast<f64>(tm.tm_sec));
}
static JSValue date_get_tz_offset(const std::vector<JSValue>&, void*) {
    return JSValue::number(0); // UTC
}
static JSValue date_to_iso_string(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    return JSValue::string(buf);
}
static JSValue date_to_utc_string(const std::vector<JSValue>&, void*) {
    auto tm = to_tm(now_ms());
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
        "Day", tm.tm_mday, "Month", tm.tm_year + 1900,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    return JSValue::string(buf);
}

void register_date_builtins(VM* vm) {
    auto* date_proto = vm->heap()->alloc_object();
    set_prototype_method(&date_proto->obj, "getTime", make_fn(vm, date_get_time));
    set_prototype_method(&date_proto->obj, "getFullYear", make_fn(vm, date_get_full_year));
    set_prototype_method(&date_proto->obj, "getMonth", make_fn(vm, date_get_month));
    set_prototype_method(&date_proto->obj, "getDate", make_fn(vm, date_get_date));
    set_prototype_method(&date_proto->obj, "getDay", make_fn(vm, date_get_day));
    set_prototype_method(&date_proto->obj, "getHours", make_fn(vm, date_get_hours));
    set_prototype_method(&date_proto->obj, "getMinutes", make_fn(vm, date_get_minutes));
    set_prototype_method(&date_proto->obj, "getSeconds", make_fn(vm, date_get_seconds));
    set_prototype_method(&date_proto->obj, "getTimezoneOffset", make_fn(vm, date_get_tz_offset));
    set_prototype_method(&date_proto->obj, "toISOString", make_fn(vm, date_to_iso_string));
    set_prototype_method(&date_proto->obj, "toUTCString", make_fn(vm, date_to_utc_string));
    auto* date_ctor = vm->heap()->alloc_object();
    date_ctor->obj.set("prototype", JSValue::object(&date_proto->obj));
    date_ctor->obj.set("now", JSValue::function(make_fn(vm, date_get_time)));
    date_ctor->obj.set("parse", JSValue::function(make_fn(vm, date_get_time)));
    vm->global_object()->set("Date", JSValue::object(&date_ctor->obj));
}

} // namespace browser::js::builtins
