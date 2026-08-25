#include "builtins.hpp"

#include <chrono>
#include <cmath>
#include <ctime>

namespace browser::js::builtins {

    static constexpr const char *kTimeField = "@time";

    static f64 now_ms() {
        return static_cast<f64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    // Reads the stored time value from a Date instance; NaN when absent/invalid.
    static f64 instance_time(const std::vector<JSValue> &args) {
        if (args.empty() || args[0].type != JSValue::Type::OBJECT || !args[0].object_val)
            return now_ms();
        JSValue t = args[0].object_val->get_property(kTimeField);
        if (t.type != JSValue::Type::NUMBER)
            return now_ms();
        return t.number_val;
    }

    static void set_instance_time(const std::vector<JSValue> &args, f64 ms) {
        if (args.empty() || args[0].type != JSValue::Type::OBJECT || !args[0].object_val)
            return;
        args[0].object_val->set(kTimeField, JSValue::number(ms));
    }

    static std::tm to_tm(f64 ms) {
        std::time_t t = static_cast<std::time_t>(ms / 1000);
        std::tm tm;
        gmtime_s(&tm, &t);
        return tm;
    }

    static constexpr const char *kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static constexpr const char *kMonths[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    static bool is_valid_time(f64 ms) {
        return !std::isnan(ms) && !std::isinf(ms);
    }

    // Parses ISO-like dates: YYYY[-MM[-DD[T HH:mm[:ss[.mmm]]]][Z]]
    static f64 parse_date_string(const std::string &s) {
        int y = 1970, mo = 1, d = 1, h = 0, mi = 0, sec = 0, msec = 0;
        int consumed = 0;
        if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d.%d%n", &y, &mo, &d, &h, &mi, &sec, &msec, &consumed) >= 3 ||
            std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d%n", &y, &mo, &d, &h, &mi, &sec, &consumed) >= 3 ||
            std::sscanf(s.c_str(), "%d-%d-%d%n", &y, &mo, &d, &consumed) == 3) {
            // Rejected forms fall through to epoch below.
        } else if (std::sscanf(s.c_str(), "%d/%d/%d%n", &mo, &d, &y, &consumed) == 3) {
            if (y < 100)
                y += 2000;
        } else {
            f64 num = 0;
            char *end = nullptr;
            num = std::strtod(s.c_str(), &end);
            if (end && end != s.c_str())
                return num;
            return std::nan("");
        }

        std::tm tm = {};
        tm.tm_year = y - 1900;
        tm.tm_mon = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = mi;
        tm.tm_sec = sec;
#ifdef _WIN32
        std::time_t tt = _mkgmtime(&tm);
#else
        std::time_t tt = timegm(&tm);
#endif
        if (tt == -1)
            return std::nan("");
        return static_cast<f64>(tt) * 1000.0 + static_cast<f64>(msec);
    }

    static JSValue date_get_time(const std::vector<JSValue> &args, void *) {
        return JSValue::number(instance_time(args));
    }

#define DATE_GETTER(name, field_expr)                                          \
    static JSValue date_get_##name(const std::vector<JSValue> &args, void *) { \
        f64 t = instance_time(args);                                           \
        if (!is_valid_time(t))                                                 \
            return JSValue::number(std::nan(""));                              \
        auto tm = to_tm(t);                                                    \
        return JSValue::number(static_cast<f64>(field_expr));                  \
    }

    DATE_GETTER(full_year, tm.tm_year + 1900)
    DATE_GETTER(month, tm.tm_mon)
    DATE_GETTER(date, tm.tm_mday)
    DATE_GETTER(day, tm.tm_wday)
    DATE_GETTER(hours, tm.tm_hour)
    DATE_GETTER(minutes, tm.tm_min)
    DATE_GETTER(seconds, tm.tm_sec)

    static JSValue date_get_milliseconds(const std::vector<JSValue> &args, void *) {
        f64 t = instance_time(args);
        if (!is_valid_time(t))
            return JSValue::number(std::nan(""));
        return JSValue::number(std::fmod(t, 1000.0));
    }

    static JSValue date_set_time(const std::vector<JSValue> &args, void *) {
        f64 v = get_number_arg(args, 1, std::nan(""));
        set_instance_time(args, v);
        return JSValue::number(v);
    }

    static JSValue date_value_of(const std::vector<JSValue> &args, void *) {
        return JSValue::number(instance_time(args));
    }

    static JSValue date_to_iso_string(const std::vector<JSValue> &args, void *) {
        f64 t = instance_time(args);
        if (!is_valid_time(t))
            return JSValue::string("Invalid Date");
        auto tm = to_tm(t);
        char buf[64];
        std::snprintf(buf,
                      sizeof(buf),
                      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                      tm.tm_year + 1900,
                      tm.tm_mon + 1,
                      tm.tm_mday,
                      tm.tm_hour,
                      tm.tm_min,
                      tm.tm_sec,
                      static_cast<int>(std::fmod(std::fabs(t), 1000.0)));
        return JSValue::string(buf);
    }

    static JSValue date_to_utc_string(const std::vector<JSValue> &args, void *) {
        f64 t = instance_time(args);
        if (!is_valid_time(t))
            return JSValue::string("Invalid Date");
        auto tm = to_tm(t);
        char buf[80];
        std::snprintf(buf,
                      sizeof(buf),
                      "%s, %02d %s %04d %02d:%02d:%02d GMT",
                      kWeekdays[tm.tm_wday],
                      tm.tm_mday,
                      kMonths[tm.tm_mon],
                      tm.tm_year + 1900,
                      tm.tm_hour,
                      tm.tm_min,
                      tm.tm_sec);
        return JSValue::string(buf);
    }

    static JSValue date_to_string(const std::vector<JSValue> &args, void *) {
        return date_to_utc_string(args, nullptr);
    }

    static JSValue date_static_now(const std::vector<JSValue> &, void *) {
        return JSValue::number(now_ms());
    }

    static JSValue date_static_parse(const std::vector<JSValue> &args, void *) {
        if (args.size() < 2 || args[1].type != JSValue::Type::STRING)
            return JSValue::number(std::nan(""));
        return JSValue::number(parse_date_string(args[1].string_val));
    }

    static JSValue date_static_utc(const std::vector<JSValue> &args, void *) {
        std::tm tm = {};
        tm.tm_year = get_int_arg(args, 1, 1970) - 1900;
        tm.tm_mon = get_int_arg(args, 2, 1) - 1;
        tm.tm_mday = get_int_arg(args, 3, 1);
        tm.tm_hour = get_int_arg(args, 4, 0);
        tm.tm_min = get_int_arg(args, 5, 0);
        tm.tm_sec = get_int_arg(args, 6, 0);
#ifdef _WIN32
        std::time_t tt = _mkgmtime(&tm);
#else
        std::time_t tt = timegm(&tm);
#endif
        f64 ms = tt == -1 ? std::nan("") : static_cast<f64>(tt) * 1000.0 + get_number_arg(args, 7, 0);
        return JSValue::number(ms);
    }

    // Constructor: new Date(), new Date(value), new Date(dateString),
    // new Date(y, m, d[, h[, m[, s[, ms]]]]). Called both with and without `new`.
    static JSValue date_ctor(const std::vector<JSValue> &args, void *) {
        // args[0] is `this` (the freshly allocated object when called via `new`);
        // component form needs at least year+month beyond that.
        f64 ms;
        if (args.size() >= 4) {
            std::tm tm = {};
            f64 yr = args[1].to_number();
            tm.tm_year = yr > 99 ? static_cast<int>(yr) - 1900 : static_cast<int>(yr) + 100;
            tm.tm_mon = static_cast<int>(args[2].to_number());
            tm.tm_mday = args.size() > 3 ? static_cast<int>(args[3].to_number()) : 1;
            tm.tm_hour = args.size() > 4 ? static_cast<int>(args[4].to_number()) : 0;
            tm.tm_min = args.size() > 5 ? static_cast<int>(args[5].to_number()) : 0;
            tm.tm_sec = args.size() > 6 ? static_cast<int>(args[6].to_number()) : 0;
#ifdef _WIN32
            std::time_t tt = _mkgmtime(&tm);
#else
            std::time_t tt = timegm(&tm);
#endif
            f64 msec = args.size() > 7 ? args[7].to_number() : 0;
            ms = tt == -1 ? std::nan("") : static_cast<f64>(tt) * 1000.0 + msec;
        } else if (args.size() == 2 && args[1].type == JSValue::Type::NUMBER) {
            ms = args[1].number_val;
        } else if (args.size() == 2 && args[1].type == JSValue::Type::STRING) {
            ms = parse_date_string(args[1].string_val);
        } else {
            ms = now_ms();
        }

        if (!args.empty() && args[0].type == JSValue::Type::OBJECT && args[0].object_val) {
            set_instance_time(args, ms);
            return JSValue::undefined();
        }
        // Called without `new`: return the raw time value.
        return JSValue::number(ms);
    }

    void register_date_builtins(VM *vm) {
        auto *date_proto = vm->heap()->alloc_object();
        set_prototype_method(&date_proto->obj, "getTime", make_fn(vm, date_get_time));
        set_prototype_method(&date_proto->obj, "getSeconds", make_fn(vm, date_get_seconds));
        set_prototype_method(&date_proto->obj, "getFullYear", make_fn(vm, date_get_full_year));
        set_prototype_method(&date_proto->obj, "getMonth", make_fn(vm, date_get_month));
        set_prototype_method(&date_proto->obj, "getDate", make_fn(vm, date_get_date));
        set_prototype_method(&date_proto->obj, "getDay", make_fn(vm, date_get_day));
        set_prototype_method(&date_proto->obj, "getHours", make_fn(vm, date_get_hours));
        set_prototype_method(&date_proto->obj, "getMinutes", make_fn(vm, date_get_minutes));
        set_prototype_method(&date_proto->obj, "getMilliseconds", make_fn(vm, date_get_milliseconds));
        set_prototype_method(&date_proto->obj, "setTime", make_fn(vm, date_set_time));
        set_prototype_method(&date_proto->obj, "valueOf", make_fn(vm, date_value_of));
        set_prototype_method(&date_proto->obj, "toISOString", make_fn(vm, date_to_iso_string));
        set_prototype_method(&date_proto->obj, "toUTCString", make_fn(vm, date_to_utc_string));
        set_prototype_method(&date_proto->obj, "toString", make_fn(vm, date_to_string));

        auto *date_ctor_fn = make_fn(vm, date_ctor, true);
        date_ctor_fn->name = "Date";
        date_ctor_fn->prototype_property = JSValue::object(&date_proto->obj);
        date_ctor_fn->properties["prototype"] = JSValue::object(&date_proto->obj);
        date_ctor_fn->properties["now"] = JSValue::function(make_fn(vm, date_static_now));
        date_ctor_fn->properties["parse"] = JSValue::function(make_fn(vm, date_static_parse));
        date_ctor_fn->properties["UTC"] = JSValue::function(make_fn(vm, date_static_utc));
        vm->global_object()->set("Date", JSValue::function(date_ctor_fn));
    }

}  // namespace browser::js::builtins
