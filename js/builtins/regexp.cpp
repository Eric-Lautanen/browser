#include "builtins.hpp"
#include "../regex_engine.hpp"

#include <string>
#include <vector>

namespace browser::js::builtins {

    struct RegexpCtx {
        VM *vm;
    };

    // ------------------------------------------------------------------
    // Shared helpers (declared in builtins.hpp, used by string.cpp)
    // ------------------------------------------------------------------

    bool value_is_regexp(const JSValue &v) {
        if (v.type != JSValue::Type::OBJECT || !v.object_val)
            return false;
        JSValue src = v.object_val->get("source");
        JSValue fl = v.object_val->get("flags");
        return src.type == JSValue::Type::STRING && fl.type == JSValue::Type::STRING;
    }

    static bool parse_flags(const std::string &fl, RegexFlags &out) {
        for (char c : fl) {
            switch (c) {
                case 'g': out.global = true; break;
                case 'i': out.ignore_case = true; break;
                case 'm': out.multiline = true; break;
                case 's': out.dot_all = true; break;
                case 'u': out.unicode = true; break;
                case 'y': out.sticky = true; break;
                case 'v': break;
                default: return false;
            }
        }
        return true;
    }

    static bool get_regex_props(const JSValue &re, std::string &source, RegexFlags &flags) {
        if (re.type != JSValue::Type::OBJECT || !re.object_val)
            return false;
        JSValue src = re.object_val->get("source");
        JSValue fl = re.object_val->get("flags");
        if (src.type != JSValue::Type::STRING || fl.type != JSValue::Type::STRING)
            return false;
        source = src.string_val;
        return parse_flags(fl.string_val, flags);
    }

    static void set_last_index(const JSValue &re, f64 v) {
        if (re.type == JSValue::Type::OBJECT && re.object_val)
            re.object_val->set("lastIndex", JSValue::number(v));
    }

    static f64 get_last_index(const JSValue &re) {
        if (re.type != JSValue::Type::OBJECT || !re.object_val)
            return 0;
        JSValue li = re.object_val->get("lastIndex");
        return li.type == JSValue::Type::NUMBER ? li.number_val : 0.0;
    }

    // Builds the exec result array: [full, groups...], props index/input.
    static JSValue make_exec_result(VM *vm, const std::string &input, const RegexMatch &m) {
        auto *arr = vm->heap()->alloc_object();
        arr->obj.is_array = true;
        arr->obj.array_elements.push_back(JSValue::string(input.substr(m.start, m.end - m.start)));
        for (size_t g = 1; g < m.cap_start.size(); g++) {
            if (m.cap_start[g] != RegexMatch::NOPOS && m.cap_end[g] != RegexMatch::NOPOS &&
                m.cap_end[g] >= m.cap_start[g] && m.cap_end[g] <= input.size()) {
                arr->obj.array_elements.push_back(
                    JSValue::string(input.substr(m.cap_start[g], m.cap_end[g] - m.cap_start[g])));
            } else {
                arr->obj.array_elements.push_back(JSValue::undefined());
            }
        }
        arr->obj.set("index", JSValue::number(static_cast<f64>(m.start)));
        arr->obj.set("input", JSValue::string(input));
        return JSValue::object(&arr->obj);
    }

    JSValue regexp_exec_at(VM *vm, const JSValue &re, const std::string &input, u32 start) {
        (void)vm;
        std::string source;
        RegexFlags flags;
        if (!get_regex_props(re, source, flags))
            return JSValue::null();
        std::string err;
        auto prog = regex_compile(source, flags, err);
        if (!prog)
            return JSValue::null();
        RegexMatch m;
        if (!regex_search(*prog, input, start, m))
            return JSValue::null();
        return make_exec_result(vm, input, m);
    }

    JSValue regexp_make(VM *vm, const std::string &source, const std::string &flags_str) {
        auto *obj_gc = vm->heap()->alloc_object();
        auto &obj = obj_gc->obj;
        JSValue ctor_val = vm->global_object()->get("RegExp");
        if (ctor_val.type == JSValue::Type::FUNCTION && ctor_val.function_val &&
            ctor_val.function_val->prototype_property.type == JSValue::Type::OBJECT) {
            obj.prototype = ctor_val.function_val->prototype_property;
        }
        obj.set("source", JSValue::string(source.empty() ? "(?:)" : source));
        obj.set("flags", JSValue::string(flags_str));
        obj.set("global", JSValue::boolean(flags_str.find('g') != std::string::npos));
        obj.set("ignoreCase", JSValue::boolean(flags_str.find('i') != std::string::npos));
        obj.set("multiline", JSValue::boolean(flags_str.find('m') != std::string::npos));
        obj.set("dotAll", JSValue::boolean(flags_str.find('s') != std::string::npos));
        obj.set("sticky", JSValue::boolean(flags_str.find('y') != std::string::npos));
        obj.set("unicode", JSValue::boolean(flags_str.find('u') != std::string::npos));
        obj.set("lastIndex", JSValue::number(0));
        return JSValue::object(&obj);
    }

    std::string regexp_expand_replacement(const std::string &tmpl,
                                          const std::string &input,
                                          const std::string &full,
                                          const std::vector<std::string> &groups,
                                          u32 index) {
        std::string out;
        for (size_t i = 0; i < tmpl.size(); i++) {
            char c = tmpl[i];
            if (c != '$' || i + 1 >= tmpl.size()) {
                out += c;
                continue;
            }
            char n = tmpl[i + 1];
            if (n == '$') {
                out += '$';
                i++;
            } else if (n == '&') {
                out += full;
                i++;
            } else if (n == '`') {
                out += input.substr(0, index);
                i++;
            } else if (n == '\'') {
                u32 tail = index + static_cast<u32>(full.size());
                out += tail <= input.size() ? input.substr(tail) : "";
                i++;
            } else if (isdigit(static_cast<unsigned char>(n))) {
                u32 g = n - '0';
                size_t j = i + 2;
                if (j < tmpl.size() && isdigit(static_cast<unsigned char>(tmpl[j]))) {
                    u32 two = g * 10 + (tmpl[j] - '0');
                    if (two >= 1 && two <= groups.size()) {
                        g = two;
                        j++;
                    }
                }
                if (g >= 1 && g <= groups.size()) {
                    out += groups[g - 1];
                    i = j - 1;
                } else {
                    out += c;
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    // ------------------------------------------------------------------
    // RegExp instance methods
    // ------------------------------------------------------------------

    static JSValue regexp_exec(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<RegexpCtx *>(context);
        VM *vm = ctx->vm;
        JSValue re = args.size() > 0 ? args[0] : JSValue::undefined();
        std::string input = args.size() > 1 ? args[1].to_string() : "";
        std::string source;
        RegexFlags flags;
        if (!get_regex_props(re, source, flags))
            return JSValue::null();
        std::string err;
        auto prog = regex_compile(source, flags, err);
        if (!prog)
            return JSValue::null();

        u32 input_size = static_cast<u32>(input.size());
        f64 li = get_last_index(re);
        u32 start = (flags.global || flags.sticky) && li >= 0 && li <= input_size
                        ? static_cast<u32>(li)
                        : 0;
        RegexMatch m;
        if (!regex_search(*prog, input, start, m)) {
            if (flags.global || flags.sticky)
                set_last_index(re, 0);
            return JSValue::null();
        }
        if (flags.global || flags.sticky)
            set_last_index(re, static_cast<f64>(m.end));
        return make_exec_result(vm, input, m);
    }

    static JSValue regexp_test(const std::vector<JSValue> &args, void *context) {
        (void)context;
        JSValue re = args.size() > 0 ? args[0] : JSValue::undefined();
        std::string input = args.size() > 1 ? args[1].to_string() : "";
        std::string source;
        RegexFlags flags;
        if (!get_regex_props(re, source, flags))
            return JSValue::boolean(false);
        std::string err;
        auto prog = regex_compile(source, flags, err);
        if (!prog)
            return JSValue::boolean(false);

        u32 input_size = static_cast<u32>(input.size());
        f64 li = get_last_index(re);
        u32 start = (flags.global || flags.sticky) && li >= 0 && li <= input_size
                        ? static_cast<u32>(li)
                        : 0;
        RegexMatch m;
        bool ok = regex_search(*prog, input, start, m);
        if (flags.global || flags.sticky)
            set_last_index(re, ok ? static_cast<f64>(m.end) : 0);
        return JSValue::boolean(ok);
    }

    static JSValue regexp_to_string(const std::vector<JSValue> &args, void *) {
        JSValue re = args.size() > 0 ? args[0] : JSValue::undefined();
        if (re.type != JSValue::Type::OBJECT || !re.object_val)
            return JSValue::string("/(?:)/");
        JSValue src = re.object_val->get("source");
        JSValue fl = re.object_val->get("flags");
        return JSValue::string("/" + (src.type == JSValue::Type::STRING ? src.string_val : "") + "/" +
                               (fl.type == JSValue::Type::STRING ? fl.string_val : ""));
    }

    static JSValue regexp_flag_method(const std::vector<JSValue> &args, void *context) {
        JSValue re = args.size() > 0 ? args[0] : JSValue::undefined();
        const char *flag = static_cast<const char *>(context);
        if (re.type != JSValue::Type::OBJECT || !re.object_val)
            return JSValue::boolean(false);
        JSValue fl = re.object_val->get("flags");
        if (fl.type != JSValue::Type::STRING)
            return JSValue::boolean(false);
        return JSValue::boolean(fl.string_val.find(flag[0]) != std::string::npos);
    }

    static JSValue regexp_constructor(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<RegexpCtx *>(context);
        VM *vm = ctx->vm;

        std::string source;
        std::string fl;
        if (args.size() >= 2 && value_is_regexp(args[1])) {
            JSValue s = args[1].object_val->get("source");
            JSValue f = args[1].object_val->get("flags");
            source = s.type == JSValue::Type::STRING ? s.string_val : "(?:)";
            fl = f.type == JSValue::Type::STRING ? f.string_val : "";
            if (args.size() >= 3 && args[2].type == JSValue::Type::STRING)
                fl = args[2].string_val;
        } else {
            source = args.size() >= 2 ? args[1].to_string() : "(?:)";
            if (source.empty())
                source = "(?:)";
            fl = args.size() >= 3 ? args[2].to_string() : "";
        }

        RegexFlags flags;
        if (!parse_flags(fl, flags)) {
            auto *bad = vm->heap()->alloc_object();
            bad->obj.set("source", JSValue::string(source));
            bad->obj.set("flags", JSValue::string(fl));
            return JSValue::object(&bad->obj);
        }

        std::string err;
        auto prog = regex_compile(source, flags, err);
        if (!prog) {
            auto *bad = vm->heap()->alloc_object();
            bad->obj.set("source", JSValue::string(source));
            bad->obj.set("flags", JSValue::string(fl));
            return JSValue::object(&bad->obj);
        }

        auto *obj_gc = vm->heap()->alloc_object();
        auto &obj = obj_gc->obj;
        JSValue ctor_val = vm->global_object()->get("RegExp");
        if (ctor_val.type == JSValue::Type::FUNCTION && ctor_val.function_val &&
            ctor_val.function_val->prototype_property.type == JSValue::Type::OBJECT) {
            obj.prototype = ctor_val.function_val->prototype_property;
        }
        obj.set("source", JSValue::string(source));
        obj.set("flags", JSValue::string(fl));
        obj.set("global", JSValue::boolean(flags.global));
        obj.set("ignoreCase", JSValue::boolean(flags.ignore_case));
        obj.set("multiline", JSValue::boolean(flags.multiline));
        obj.set("dotAll", JSValue::boolean(flags.dot_all));
        obj.set("sticky", JSValue::boolean(flags.sticky));
        obj.set("unicode", JSValue::boolean(flags.unicode));
        obj.set("lastIndex", JSValue::number(0));
        return JSValue::object(&obj);
    }

    void register_regexp_builtins(VM *vm) {
        auto *ctx = new RegexpCtx{vm};

        auto *regexp_proto = vm->heap()->alloc_object();
        set_prototype_method(&regexp_proto->obj, "exec", make_fn(vm, regexp_exec, false, ctx));
        set_prototype_method(&regexp_proto->obj, "test", make_fn(vm, regexp_test, false, ctx));
        set_prototype_method(&regexp_proto->obj, "toString", make_fn(vm, regexp_to_string, false, ctx));
        set_prototype_method(&regexp_proto->obj, "isGlobal", make_fn(vm, regexp_flag_method, false,
                                                                    const_cast<char *>("g")));
        set_prototype_method(&regexp_proto->obj, "isIgnoreCase", make_fn(vm, regexp_flag_method, false,
                                                                        const_cast<char *>("i")));
        set_prototype_method(&regexp_proto->obj, "isMultiline", make_fn(vm, regexp_flag_method, false,
                                                                       const_cast<char *>("m")));
        set_prototype_method(&regexp_proto->obj, "isDotAll", make_fn(vm, regexp_flag_method, false,
                                                                    const_cast<char *>("s")));
        set_prototype_method(&regexp_proto->obj, "isSticky", make_fn(vm, regexp_flag_method, false,
                                                                    const_cast<char *>("y")));

        auto *regexp_fn = vm->create_native_fn(regexp_constructor, true, ctx);
        regexp_fn->name = "RegExp";
        regexp_fn->prototype_property = JSValue::object(&regexp_proto->obj);
        regexp_fn->properties["prototype"] = JSValue::object(&regexp_proto->obj);
        vm->global_object()->set("RegExp", JSValue::function(regexp_fn));
    }

}  // namespace browser::js::builtins
