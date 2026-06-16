#include "builtins.hpp"

namespace browser::js::builtins {

static JSValue string_char_at(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    i32 pos = get_int_arg(args, 1, 0);
    if (pos < 0 || pos >= static_cast<i32>(s.size())) return JSValue::string("");
    return JSValue::string(std::string(1, s[pos]));
}

static JSValue string_char_code_at(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    i32 pos = get_int_arg(args, 1, 0);
    if (pos < 0 || pos >= static_cast<i32>(s.size())) return JSValue::number(NAN);
    return JSValue::number(static_cast<f64>(static_cast<unsigned char>(s[pos])));
}

static JSValue string_index_of(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string search = args.size() > 1 ? args[1].to_string() : "";
    i32 from = get_int_arg(args, 2, 0);
    if (from < 0) from = 0;
    auto pos = s.find(search, from);
    if (pos == std::string::npos) return JSValue::number(-1);
    return JSValue::number(static_cast<f64>(pos));
}

static JSValue string_last_index_of(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string search = args.size() > 1 ? args[1].to_string() : "";
    auto pos = s.rfind(search);
    if (pos == std::string::npos) return JSValue::number(-1);
    return JSValue::number(static_cast<f64>(pos));
}

static JSValue string_includes(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string search = args.size() > 1 ? args[1].to_string() : "";
    return JSValue::boolean(s.find(search) != std::string::npos);
}

static JSValue string_starts_with(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string search = args.size() > 1 ? args[1].to_string() : "";
    if (search.size() > s.size()) return JSValue::boolean(false);
    return JSValue::boolean(s.substr(0, search.size()) == search);
}

static JSValue string_ends_with(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string search = args.size() > 1 ? args[1].to_string() : "";
    if (search.size() > s.size()) return JSValue::boolean(false);
    return JSValue::boolean(s.substr(s.size() - search.size()) == search);
}

static JSValue string_slice(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    i32 len = static_cast<i32>(s.size());
    i32 start = get_int_arg(args, 1, 0);
    i32 end_val = args.size() > 2 ? get_int_arg(args, 2) : len;
    if (start < 0) start = std::max(0, len + start);
    if (end_val < 0) end_val = std::max(0, len + end_val);
    if (start >= len || start >= end_val) return JSValue::string("");
    if (end_val > len) end_val = len;
    return JSValue::string(s.substr(start, end_val - start));
}

static JSValue string_substring(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    i32 len = static_cast<i32>(s.size());
    i32 start = static_cast<i32>(clamp(get_int_arg(args, 1, 0), 0, len));
    i32 end_val = args.size() > 2 ? static_cast<i32>(clamp(get_int_arg(args, 2), 0, len)) : len;
    if (start > end_val) std::swap(start, end_val);
    return JSValue::string(s.substr(start, end_val - start));
}

static JSValue string_substr(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    i32 len = static_cast<i32>(s.size());
    i32 start = get_int_arg(args, 1, 0);
    i32 count = args.size() > 2 ? get_int_arg(args, 2) : len;
    if (start < 0) start = std::max(0, len + start);
    if (count < 0) count = 0;
    return JSValue::string(s.substr(start, count));
}

static JSValue string_to_upper(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string result;
    result.reserve(s.size());
    for (char c : s) result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return JSValue::string(result);
}

static JSValue string_to_lower(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string result;
    result.reserve(s.size());
    for (char c : s) result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return JSValue::string(result);
}

static JSValue string_trim(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    auto start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return JSValue::string("");
    auto end = s.find_last_not_of(" \t\n\r\f\v");
    return JSValue::string(s.substr(start, end - start + 1));
}

static JSValue string_trim_start(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    auto start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return JSValue::string("");
    return JSValue::string(s.substr(start));
}

static JSValue string_trim_end(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    auto end = s.find_last_not_of(" \t\n\r\f\v");
    if (end == std::string::npos) return JSValue::string("");
    return JSValue::string(s.substr(0, end + 1));
}

struct StringCtx { VM* vm; };

static JSValue string_split(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<StringCtx*>(context);
    std::string s = args[0].to_string();
    auto* arr_gc = ctx->vm->heap()->alloc_object();
    arr_gc->obj.is_array = true;
    auto& elements = arr_gc->obj.array_elements;
    if (args.size() < 2) {
        elements.push_back(JSValue::string(s));
        return JSValue::object(&arr_gc->obj);
    }
    std::string sep = args[1].to_string();
    if (sep.empty()) {
        for (char c : s) elements.push_back(JSValue::string(std::string(1, c)));
        return JSValue::object(&arr_gc->obj);
    }
    size_t start = 0, end;
    while ((end = s.find(sep, start)) != std::string::npos) {
        elements.push_back(JSValue::string(s.substr(start, end - start)));
        start = end + sep.size();
    }
    elements.push_back(JSValue::string(s.substr(start)));
    return JSValue::object(&arr_gc->obj);
}

static JSValue string_repeat(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    i32 count = get_int_arg(args, 1, 0);
    if (count <= 0) return JSValue::string("");
    std::string result;
    result.reserve(s.size() * count);
    for (i32 i = 0; i < count; i++) result += s;
    return JSValue::string(result);
}

static JSValue string_pad_start(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    i32 target_len = get_int_arg(args, 1, 0);
    std::string pad_str = args.size() > 2 ? args[2].to_string() : " ";
    if (target_len <= static_cast<i32>(s.size())) return JSValue::string(s);
    i32 pad_needed = target_len - static_cast<i32>(s.size());
    std::string result;
    result.reserve(target_len);
    while (static_cast<i32>(result.size()) < pad_needed) result += pad_str;
    result = result.substr(0, pad_needed) + s;
    return JSValue::string(result);
}

static JSValue string_pad_end(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    i32 target_len = get_int_arg(args, 1, 0);
    std::string pad_str = args.size() > 2 ? args[2].to_string() : " ";
    if (target_len <= static_cast<i32>(s.size())) return JSValue::string(s);
    std::string result = s;
    while (static_cast<i32>(result.size()) < target_len) result += pad_str;
    result = result.substr(0, target_len);
    return JSValue::string(result);
}

static JSValue string_concat(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    for (u32 i = 1; i < args.size(); i++) s += args[i].to_string();
    return JSValue::string(s);
}

static JSValue string_replace(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string search = args.size() > 1 ? args[1].to_string() : "";
    std::string repl = args.size() > 2 ? args[2].to_string() : "";
    auto pos = s.find(search);
    if (pos == std::string::npos) return JSValue::string(s);
    return JSValue::string(s.substr(0, pos) + repl + s.substr(pos + search.size()));
}

static JSValue string_replace_all(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string search = args.size() > 1 ? args[1].to_string() : "";
    std::string repl = args.size() > 2 ? args[2].to_string() : "";
    if (search.empty()) return JSValue::string(s);
    std::string result;
    size_t start = 0, pos;
    while ((pos = s.find(search, start)) != std::string::npos) {
        result += s.substr(start, pos - start) + repl;
        start = pos + search.size();
    }
    result += s.substr(start);
    return JSValue::string(result);
}

static JSValue string_search(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string pattern = args.size() > 1 ? args[1].to_string() : "";
    auto pos = s.find(pattern);
    if (pos == std::string::npos) return JSValue::number(-1);
    return JSValue::number(static_cast<f64>(pos));
}

static JSValue string_match(const std::vector<JSValue>& args, void*) {
    std::string s = args[0].to_string();
    std::string pattern = args.size() > 1 ? args[1].to_string() : "";
    auto pos = s.find(pattern);
    if (pos == std::string::npos) return JSValue::null();
    return JSValue::string(s.substr(pos, pattern.size()));
}

void register_string_prototype(VM* vm) {
    auto* string_proto = vm->heap()->alloc_object();
    auto* ctx = new StringCtx{vm};
    set_prototype_method(&string_proto->obj, "charAt", make_fn(vm, string_char_at));
    set_prototype_method(&string_proto->obj, "charCodeAt", make_fn(vm, string_char_code_at));
    set_prototype_method(&string_proto->obj, "indexOf", make_fn(vm, string_index_of));
    set_prototype_method(&string_proto->obj, "lastIndexOf", make_fn(vm, string_last_index_of));
    set_prototype_method(&string_proto->obj, "includes", make_fn(vm, string_includes));
    set_prototype_method(&string_proto->obj, "startsWith", make_fn(vm, string_starts_with));
    set_prototype_method(&string_proto->obj, "endsWith", make_fn(vm, string_ends_with));
    set_prototype_method(&string_proto->obj, "slice", make_fn(vm, string_slice));
    set_prototype_method(&string_proto->obj, "substring", make_fn(vm, string_substring));
    set_prototype_method(&string_proto->obj, "substr", make_fn(vm, string_substr));
    set_prototype_method(&string_proto->obj, "toUpperCase", make_fn(vm, string_to_upper));
    set_prototype_method(&string_proto->obj, "toLowerCase", make_fn(vm, string_to_lower));
    set_prototype_method(&string_proto->obj, "trim", make_fn(vm, string_trim));
    set_prototype_method(&string_proto->obj, "trimStart", make_fn(vm, string_trim_start));
    set_prototype_method(&string_proto->obj, "trimEnd", make_fn(vm, string_trim_end));
    set_prototype_method(&string_proto->obj, "split", make_fn(vm, string_split, false, ctx));
    set_prototype_method(&string_proto->obj, "replace", make_fn(vm, string_replace));
    set_prototype_method(&string_proto->obj, "replaceAll", make_fn(vm, string_replace_all));
    set_prototype_method(&string_proto->obj, "search", make_fn(vm, string_search));
    set_prototype_method(&string_proto->obj, "match", make_fn(vm, string_match));
    set_prototype_method(&string_proto->obj, "repeat", make_fn(vm, string_repeat));
    set_prototype_method(&string_proto->obj, "padStart", make_fn(vm, string_pad_start));
    set_prototype_method(&string_proto->obj, "padEnd", make_fn(vm, string_pad_end));
    set_prototype_method(&string_proto->obj, "concat", make_fn(vm, string_concat));
    string_proto->obj.set("length", JSValue::number(0));
    auto* str_ctor_gc = vm->heap()->alloc_object();
    str_ctor_gc->obj.set("prototype", JSValue::object(&string_proto->obj));
    vm->global_object()->set("String", JSValue::object(&str_ctor_gc->obj));
}

} // namespace browser::js::builtins
