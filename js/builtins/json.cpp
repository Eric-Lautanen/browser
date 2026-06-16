#include "builtins.hpp"
#include <sstream>

namespace browser::js::builtins {

struct JsonCtx { VM* vm; };

static std::string json_escape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    r += '"';
    for (char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\b': r += "\\b"; break;
            case '\f': r += "\\f"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default: r += c;
        }
    }
    r += '"';
    return r;
}

static void json_stringify_value(std::string& out, const JSValue& val, const std::string& indent, i32 depth) {
    switch (val.type) {
        case JSValue::Type::NULL_VAL: out += "null"; break;
        case JSValue::Type::UNDEFINED: out += "null"; break;
        case JSValue::Type::BOOLEAN: out += val.bool_val ? "true" : "false"; break;
        case JSValue::Type::NUMBER: {
            if (std::isnan(val.number_val) || std::isinf(val.number_val)) { out += "null"; break; }
            out += val.to_string();
            break;
        }
        case JSValue::Type::STRING: out += json_escape(val.string_val); break;
        case JSValue::Type::OBJECT: {
            if (!val.object_val) { out += "null"; break; }
            if (val.object_val->is_array) {
                out += "[";
                for (u32 i = 0; i < val.object_val->array_elements.size(); i++) {
                    if (i > 0) out += ",";
                    json_stringify_value(out, val.object_val->array_elements[i], indent, depth + 1);
                }
                out += "]";
            } else {
                out += "{";
                bool first = true;
                for (auto& [k, v] : val.object_val->properties) {
                    if (!first) out += ",";
                    first = false;
                    out += json_escape(k) + ":";
                    json_stringify_value(out, v, indent, depth + 1);
                }
                out += "}";
            }
            break;
        }
        case JSValue::Type::FUNCTION: out += "null"; break;
    }
}

static JSValue json_stringify(const std::vector<JSValue>& args, void*) {
    if (args.size() < 2) return JSValue::undefined();
    std::string out;
    json_stringify_value(out, args[1], "", 0);
    return JSValue::string(out);
}

static JSValue json_parse(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<JsonCtx*>(context);
    if (args.size() < 2) return JSValue::undefined();
    std::string s = args[1].to_string();
    size_t pos = 0;
    // Simple recursive descent JSON parser
    std::function<JSValue()> parse_value = [&]() -> JSValue {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
        if (pos >= s.size()) return JSValue::undefined();
        if (s[pos] == '"') {
            std::string str;
            pos++; // skip "
            while (pos < s.size() && s[pos] != '"') {
                if (s[pos] == '\\') {
                    pos++;
                    if (pos >= s.size()) break;
                    switch (s[pos]) {
                        case '"': str += '"'; break;
                        case '\\': str += '\\'; break;
                        case '/': str += '/'; break;
                        case 'b': str += '\b'; break;
                        case 'f': str += '\f'; break;
                        case 'n': str += '\n'; break;
                        case 'r': str += '\r'; break;
                        case 't': str += '\t'; break;
                        case 'u': {
                            if (pos + 4 < s.size()) {
                                std::string hex = s.substr(pos + 1, 4);
                                char32_t cp = std::strtol(hex.c_str(), nullptr, 16);
                                if (cp <= 0x7F) str += static_cast<char>(cp);
                                else if (cp <= 0x7FF) { str += static_cast<char>(0xC0 | (cp >> 6)); str += static_cast<char>(0x80 | (cp & 0x3F)); }
                                else { str += static_cast<char>(0xE0 | (cp >> 12)); str += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); str += static_cast<char>(0x80 | (cp & 0x3F)); }
                                pos += 4;
                            }
                            break;
                        }
                        default: str += s[pos]; break;
                    }
                } else {
                    str += s[pos];
                }
                pos++;
            }
            if (pos < s.size()) pos++; // skip "
            return JSValue::string(str);
        }
        if (s.substr(pos, 4) == "true") { pos += 4; return JSValue::boolean(true); }
        if (s.substr(pos, 5) == "false") { pos += 5; return JSValue::boolean(false); }
        if (s.substr(pos, 4) == "null") { pos += 4; return JSValue::null(); }
        if (s[pos] == '-' || s[pos] == '+' || (s[pos] >= '0' && s[pos] <= '9')) {
            size_t start = pos;
            if (s[pos] == '-' || s[pos] == '+') pos++;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            if (pos < s.size() && s[pos] == '.') {
                pos++;
                while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            }
            if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
                pos++;
                if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) pos++;
                while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            }
            return JSValue::number(std::strtod(s.substr(start, pos - start).c_str(), nullptr));
        }
        if (s[pos] == '[') {
            pos++; // skip [
            auto* arr_gc = ctx->vm->heap()->alloc_object();
            arr_gc->obj.is_array = true;
            auto& el = arr_gc->obj.array_elements;
            while (pos < s.size() && s[pos] != ']') {
                while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
                if (s[pos] == ']') break;
                el.push_back(parse_value());
                while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
                if (s[pos] == ',') { pos++;
                    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++; }
            }
            if (pos < s.size()) pos++;
            return JSValue::object(&arr_gc->obj);
        }
        if (s[pos] == '{') {
            pos++;
            auto* obj_gc = ctx->vm->heap()->alloc_object();
            JSObject* obj = &obj_gc->obj;
            while (pos < s.size() && s[pos] != '}') {
                while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
                if (s[pos] == '}') break;
                JSValue key = parse_value();
                while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
                if (s[pos] == ':') pos++;
                while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
                JSValue val = parse_value();
                obj->set(key.to_string(), val);
                while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
                if (s[pos] == ',') { pos++;
                    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++; }
            }
            if (pos < s.size()) pos++;
            return JSValue::object(obj);
        }
        return JSValue::undefined();
    };
    return parse_value();
}

void register_json_builtins(VM* vm) {
    auto* ctx = new JsonCtx{vm};
    auto* json_obj = vm->heap()->alloc_object();
    json_obj->obj.set("stringify", JSValue::function(make_fn(vm, json_stringify)));
    json_obj->obj.set("parse", JSValue::function(make_fn(vm, json_parse, false, ctx)));
    vm->global_object()->set("JSON", JSValue::object(&json_obj->obj));
}

} // namespace browser::js::builtins
