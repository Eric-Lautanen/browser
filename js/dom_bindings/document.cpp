#include "../dom_bindings.hpp"
#include "../vm.hpp"
#include "../gc.hpp"
#include "../../html/dom.hpp"
#include "../../html/traversal.hpp"
#include "../../html/parser.hpp"
#include "../../net/cookie_jar.hpp"
#include "../../net/url.hpp"
#include "../../net/http_client.hpp"
#include <cctype>
#include <cstdlib>
#include <chrono>

namespace browser::js::dom_bindings {

struct DocCtx {
    html::Element* element;
    DOMBindings* bindings;
    VM* vm;
    std::string page_url;
};

static JSValue doc_get_element_by_id(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    if (args.size() < 2) return JSValue::null();
    std::string id = args[1].to_string();
    html::Element* found = nullptr;
    html::traverse_depth_first(ctx->element, [&](html::Node* node) {
        if (!found && node->type == html::NodeType::ELEMENT) {
            auto* el = static_cast<html::Element*>(node);
            if (el->has_attribute("id") && el->get_attribute("id") == id) found = el;
        }
    });
    if (found) return ctx->bindings->wrap_element(found, ctx->vm);
    return JSValue::null();
}

static JSValue doc_create_element(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    if (args.size() < 2) return JSValue::null();
    std::string tag = args[1].to_string();
    auto el = html::create_element(tag);
    auto* el_ptr = el.release();
    html::append_child(ctx->element, std::unique_ptr<html::Node>(el_ptr));
    return ctx->bindings->wrap_element(el_ptr, ctx->vm);
}

static JSValue doc_create_text_node(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    if (args.size() < 2) return JSValue::null();
    std::string text = args[1].to_string();
    auto txt = html::create_text(text);
    auto* txt_ptr = txt.release();
    html::append_child(ctx->element, std::unique_ptr<html::Node>(txt_ptr));
    return ctx->bindings->wrap_element(static_cast<html::Element*>(txt_ptr), ctx->vm);
}

static JSValue doc_query_selector(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    if (args.empty()) return JSValue::null();
    std::string sel = args[1].to_string();
    auto* found = html::find_element_by_tag(ctx->element, sel);
    if (found) return ctx->bindings->wrap_element(found, ctx->vm);
    return JSValue::null();
}

static JSValue doc_write(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    for (u32 i = 1; i < args.size(); i++) {
        std::string html_str = args[i].to_string();
        // Simple: parse HTML fragment and append to body
        auto parsed = html::parse_fragment(html_str);
        if (parsed) {
            for (auto& child : parsed->children) {
                auto* ptr = child.release();
                html::append_child(ctx->element, std::unique_ptr<html::Node>(ptr));
            }
        }
    }
    return JSValue::undefined();
}

static JSValue doc_get_title(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    auto* title_el = html::find_element_by_tag(ctx->element, "title");
    if (title_el) return JSValue::string(html::inner_text(title_el));
    return JSValue::string("");
}

static JSValue doc_set_title(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    if (args.size() < 2) return JSValue::undefined();
    // Find/create title element
    auto* title_el = html::find_element_by_tag(ctx->element, "title");
    if (!title_el) {
        auto t = html::create_element("title");
        title_el = t.release();
        auto* head = html::find_element_by_tag(ctx->element, "head");
        if (head) html::append_child(head, std::unique_ptr<html::Node>(title_el));
    }
    // Clear children and add text
    title_el->children.clear();
    auto txt = html::create_text(args[1].to_string());
    html::append_child(title_el, std::move(txt));
    return JSValue::undefined();
}

static JSValue doc_get_cookie(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    if (ctx->page_url.empty()) return JSValue::string("");

    auto url_r = net::URL::parse(ctx->page_url);
    if (url_r.is_err()) return JSValue::string("");
    auto& url = url_r.unwrap();

    bool secure = (url.scheme == "https");
    auto& jar = net::HTTPClient::cookie_jar();
    auto cookies = jar.get_cookies_for_js(url.host, url.path, secure);

    std::string result;
    for (size_t i = 0; i < cookies.size(); i++) {
        if (i > 0) result += "; ";
        result += cookies[i].name + "=" + cookies[i].value;
    }
    return JSValue::string(result);
}

static JSValue doc_set_cookie(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<DocCtx*>(context);
    if (args.size() < 2) return JSValue::undefined();
    if (ctx->page_url.empty()) return JSValue::undefined();

    std::string cookie_str = args[1].to_string();
    auto url_r = net::URL::parse(ctx->page_url);
    if (url_r.is_err()) return JSValue::undefined();
    auto& url = url_r.unwrap();

    // Parse "name=value[;attr=val...]"
    auto eq = cookie_str.find('=');
    if (eq == std::string::npos) return JSValue::undefined();

    std::string name = cookie_str.substr(0, eq);
    std::string rest = cookie_str.substr(eq + 1);

    // Split rest on ';' - first part is value, rest are attributes
    net::Cookie c;
    c.name = name;
    c.creation_time = static_cast<u64>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    c.last_access_time = c.creation_time;
    c.domain = url.host;
    c.path = "/";

    std::string value;
    auto semi = rest.find(';');
    if (semi == std::string::npos) {
        value = rest;
    } else {
        value = rest.substr(0, semi);
        std::string attrs_str = rest.substr(semi + 1);

        // Parse attributes
        size_t pos = 0;
        while (pos < attrs_str.size()) {
            while (pos < attrs_str.size() && (attrs_str[pos] == ' ' || attrs_str[pos] == '\t')) pos++;
            if (pos >= attrs_str.size()) break;
            size_t end = attrs_str.find(';', pos);
            if (end == std::string::npos) end = attrs_str.size();
            std::string attr = attrs_str.substr(pos, end - pos);
            pos = end + 1;

            auto attr_eq = attr.find('=');
            std::string attr_name, attr_val;
            if (attr_eq != std::string::npos) {
                attr_name = attr.substr(0, attr_eq);
                attr_val = attr.substr(attr_eq + 1);
            } else {
                attr_name = attr;
            }

            std::string lc_name;
            for (char ch : attr_name) lc_name += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (lc_name == "domain") {
                c.domain = attr_val;
            } else if (lc_name == "path") {
                if (!attr_val.empty() && attr_val.front() == '/')
                    c.path = attr_val;
                else
                    c.path = "/";
            } else if (lc_name == "secure") {
                c.secure = true;
            } else if (lc_name == "httponly") {
                c.httpOnly = true;
            } else if (lc_name == "max-age") {
                char* endp = nullptr;
                long max_age = std::strtol(attr_val.c_str(), &endp, 10);
                if (endp != attr_val.c_str() && max_age > 0)
                    c.expires_time = c.creation_time + static_cast<u64>(max_age);
            } else if (lc_name == "samesite") {
                std::string lc_val;
                for (char ch : attr_val) lc_val += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lc_val == "lax" || lc_val == "strict" || lc_val == "none")
                    c.sameSite = lc_val;
            }
        }
    }

    // Trim whitespace from name and value
    auto trim_end = [](std::string& s) {
        size_t end = s.size();
        while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
        s.resize(end);
    };
    trim_end(name);
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(0, 1);
    trim_end(value);

    c.name = name;
    c.value = value;

    auto& jar = net::HTTPClient::cookie_jar();
    jar.set_cookie(url.host, url.path, c);

    return JSValue::undefined();
}

void register_document_bindings(VM* vm, DOMBindings* bindings, html::Element* document_element,
                                const std::string& page_url) {
    auto* ctx = new DocCtx{document_element, bindings, vm, page_url};
    JSValue doc_wrapper = bindings->wrap_element(document_element, vm);
    auto* doc_obj = doc_wrapper.object_val;
    
    doc_obj->set("getElementById", JSValue::function(vm->create_native_fn(doc_get_element_by_id, false, ctx)));
    doc_obj->set("createElement", JSValue::function(vm->create_native_fn(doc_create_element, false, ctx)));
    doc_obj->set("createTextNode", JSValue::function(vm->create_native_fn(doc_create_text_node, false, ctx)));
    doc_obj->set("querySelector", JSValue::function(vm->create_native_fn(doc_query_selector, false, ctx)));
    doc_obj->set("write", JSValue::function(vm->create_native_fn(doc_write, false, ctx)));
    doc_obj->set("writeln", JSValue::function(vm->create_native_fn(doc_write, false, ctx)));
    doc_obj->set("title", JSValue::string(""));
    // document.cookie getter/setter (regular property - getter/setter requires engine support)
    doc_obj->set("cookie", JSValue::string(""));
    vm->global_object()->set("document", doc_wrapper);
}

} // namespace browser::js::dom_bindings
