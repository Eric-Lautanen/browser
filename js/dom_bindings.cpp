#include "dom_bindings.hpp"
#include "vm.hpp"
#include "gc.hpp"
#include "../css/parser.hpp"
#include "../css/selector_match.hpp"
#include "../html/traversal.hpp"

#include <algorithm>

namespace browser::js {

ElementExtender DOMBindings::element_extender_ = nullptr;

DOMBindings::DOMBindings() = default;

DOMBindings::~DOMBindings() {
    *gc_alive_ = false;
}

NativeCallContext* DOMBindings::make_context(html::Element* el, VM* vm) {
    auto ctx = std::make_unique<NativeCallContext>();
    ctx->element = el;
    ctx->bindings = this;
    ctx->vm = vm;
    auto* ptr = ctx.get();
    contexts_.push_back(std::move(ctx));
    return ptr;
}

void DOMBindings::set_up_element_methods(JSObject* obj, html::Element* el, VM* vm) {
    auto* ctx = make_context(el, vm);

    obj->set("getInnerHTML", JSValue::function(vm->create_native_fn(native_get_inner_html, false, ctx)));
    obj->set("getAttribute", JSValue::function(vm->create_native_fn(native_get_attribute, false, ctx)));
    obj->set("setAttribute", JSValue::function(vm->create_native_fn(native_set_attribute, false, ctx)));
    obj->set("appendChild", JSValue::function(vm->create_native_fn(native_append_child, false, ctx)));
    obj->set("removeChild", JSValue::function(vm->create_native_fn(native_remove_child, false, ctx)));
    obj->set("querySelector", JSValue::function(vm->create_native_fn(native_query_selector, false, ctx)));
    obj->set("querySelectorAll", JSValue::function(vm->create_native_fn(native_query_selector_all, false, ctx)));
    obj->set("textContent", JSValue::function(vm->create_native_fn(native_get_text_content, false, ctx)));
    obj->set("setTextContent", JSValue::function(vm->create_native_fn(native_set_text_content, false, ctx)));
    obj->set("addEventListener", JSValue::function(vm->create_native_fn(native_add_event_listener, false, ctx)));

    if (element_extender_)
        element_extender_(this, vm, el);
}

void DOMBindings::set_up_document_methods(JSObject* obj, html::Element* el, VM* vm) {
    auto* ctx = make_context(el, vm);
    obj->set("getElementById", JSValue::function(vm->create_native_fn(native_get_element_by_id, false, ctx)));
    obj->set("createElement", JSValue::function(vm->create_native_fn(native_create_element, false, ctx)));
    obj->set("createTextNode", JSValue::function(vm->create_native_fn(native_create_text_node, false, ctx)));
    obj->set("querySelector", JSValue::function(vm->create_native_fn(native_query_selector, false, ctx)));
    obj->set("querySelectorAll", JSValue::function(vm->create_native_fn(native_query_selector_all, false, ctx)));

    // document.body / documentElement / title snapshots (set at registration;
    // scripts run after the document is parsed).
    auto *body = html::find_element_by_tag(el, "body");
    if (body)
        obj->set("body", wrap_element(body, vm));
    obj->set("documentElement", wrap_element(el, vm));
    auto *title_el = html::find_element_by_tag(el, "title");
    obj->set("title", JSValue::string(title_el ? html::inner_text(title_el) : ""));
}

JSValue DOMBindings::wrap_node(html::Node* node, VM* vm) {
    if (!node) return JSValue::null();
    auto* existing = get_dom_wrapper(node);
    if (existing) return JSValue::object(existing);

    auto* gc_obj = vm->heap()->alloc_object();
    auto* obj = &gc_obj->obj;

    wrappers_.node_to_wrapper[node] = gc_obj;
    wrappers_.wrapper_to_node[&gc_obj->obj] = node;

    if (node->type == html::NodeType::ELEMENT)
        set_up_element_methods(obj, static_cast<html::Element*>(node), vm);

    return JSValue::object(obj);
}

JSValue DOMBindings::wrap_element(html::Element* element, VM* vm) {
    return wrap_node(element, vm);
}

JSObject* DOMBindings::get_dom_wrapper(html::Node* node) const {
    auto it = wrappers_.node_to_wrapper.find(node);
    if (it != wrappers_.node_to_wrapper.end())
        return &it->second->obj;
    return nullptr;
}

html::Node *DOMBindings::get_node_from_wrapper(JSObject *wrapper) const {
    auto it = wrappers_.wrapper_to_node.find(wrapper);
    if (it != wrappers_.wrapper_to_node.end())
        return it->second;
    return nullptr;
}

void DOMBindings::add_event_listener(html::Node* node, const std::string& event_type, JSValue handler) {
    event_listeners_[node].push_back({event_type, handler});
}

std::vector<DOMBindings::ListenerEntry> DOMBindings::get_event_listeners(html::Node* node) const {
    auto it = event_listeners_.find(node);
    if (it != event_listeners_.end()) return it->second;
    return {};
}

void DOMBindings::fire_event(html::Node* node, const std::string& event_type, VM* vm) {
    auto listeners = get_event_listeners(node);
    for (auto& entry : listeners) {
        if (entry.type != event_type) continue;
        auto& handler = entry.handler;
        if (handler.type == JSValue::Type::FUNCTION && handler.function_val) {
            // J-M5: dispatch through invoke() so bytecode handlers run in a
            // contained call frame, and keep the GC suspended while we hold
            // handler values only in C++ locals.
            VM::NativeCallScope gc_guard(*vm);
            vm->invoke(handler, {});
        }
    }
}

std::vector<JSValue*> DOMBindings::gc_roots() {
    gc_stable_.clear();

    size_t total = wrappers_.node_to_wrapper.size();
    for (auto& [n, listeners] : event_listeners_) {
        total += listeners.size();
    }
    gc_stable_.reserve(total);

    std::vector<JSValue*> roots;
    roots.reserve(total);

    for (auto &[node, gc_obj] : wrappers_.node_to_wrapper) {
        gc_stable_.push_back(JSValue::object(&gc_obj->obj));
        roots.push_back(&gc_stable_.back());
    }

    for (auto& [node, listeners] : event_listeners_) {
        for (auto& entry : listeners) {
            gc_stable_.push_back(entry.handler);
            roots.push_back(&gc_stable_.back());
        }
    }

    return roots;
}

void DOMBindings::register_dom_bindings(VM* vm, html::Element* document_element) {
    JSValue doc_wrapper = wrap_element(document_element, vm);
    auto* doc_obj = doc_wrapper.object_val;
    set_up_document_methods(doc_obj, document_element, vm);
    vm->global_object()->set("document", doc_wrapper);

    auto* window_gc = vm->heap()->alloc_object();
    window_gc->obj.set("document", doc_wrapper);
    vm->global_object()->set("window", JSValue::object(&window_gc->obj));

    // window.location: function-based bindings (plain property assignment
    // cannot be intercepted on JSObject yet).
    auto *loc_gc = vm->heap()->alloc_object();
    {
        auto *lctx = make_context(document_element, vm);
        // href is a data property so `location.href` reads work; assignment
        // is not interceptable on plain objects — use setHref/assign.
        loc_gc->obj.set("href", JSValue::string(page_url_));
        loc_gc->obj.set("setHref", JSValue::function(vm->create_native_fn(native_location_set_href, false, lctx)));
        loc_gc->obj.set("assign", JSValue::function(vm->create_native_fn(native_location_assign, false, lctx)));
        loc_gc->obj.set("replace", JSValue::function(vm->create_native_fn(native_location_assign, false, lctx)));
        loc_gc->obj.set("reload", JSValue::function(vm->create_native_fn(native_location_reload, false, lctx)));
    }
    window_gc->obj.set("location", JSValue::object(&loc_gc->obj));
    vm->global_object()->set("location", JSValue::object(&loc_gc->obj));

    // window.navigator: static environment data.
    auto *nav_gc = vm->heap()->alloc_object();
    {
        nav_gc->obj.set("userAgent", JSValue::string("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Browser/0.1"));
        nav_gc->obj.set("appName", JSValue::string("Browser"));
        nav_gc->obj.set("appVersion", JSValue::string("0.1"));
        nav_gc->obj.set("platform", JSValue::string("Win32"));
        nav_gc->obj.set("language", JSValue::string("en-US"));
        nav_gc->obj.set("languages", JSValue::string("en-US,en"));
        nav_gc->obj.set("onLine", JSValue::boolean(true));
    }
    window_gc->obj.set("navigator", JSValue::object(&nav_gc->obj));
    vm->global_object()->set("navigator", JSValue::object(&nav_gc->obj));

    vm->add_gc_root_provider([this, alive = gc_alive_]() -> std::vector<JSValue*> {
        if (!*alive) return {};
        return gc_roots();
    });
}
JSValue DOMBindings::native_location_href(const std::vector<JSValue> &args, void *context) {
    (void)args;
    auto *ctx = static_cast<NativeCallContext *>(context);
    return JSValue::string(ctx->bindings->page_url());
}

static void navigate_with(JSValue arg, DOMBindings *bindings) {
    if (arg.type != JSValue::Type::STRING)
        return;
    bindings->navigate_to(arg.string_val);
}

JSValue DOMBindings::native_location_set_href(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<NativeCallContext *>(context);
    if (args.size() >= 2)
        navigate_with(args[1], ctx->bindings);
    return JSValue::undefined();
}

JSValue DOMBindings::native_location_assign(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<NativeCallContext *>(context);
    if (args.size() >= 2)
        navigate_with(args[1], ctx->bindings);
    return JSValue::undefined();
}

JSValue DOMBindings::native_location_reload(const std::vector<JSValue> &args, void *context) {
    (void)args;
    auto *ctx = static_cast<NativeCallContext *>(context);
    if (!ctx->bindings->page_url().empty())
        ctx->bindings->navigate_to(ctx->bindings->page_url());
    return JSValue::undefined();
}

JSValue DOMBindings::native_get_inner_html(const std::vector<JSValue>&, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    return JSValue::string(html::serialize_dom(ctx->element));
}

JSValue DOMBindings::native_get_attribute(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    // args[0] = this, args[1] = first real arg
    if (args.size() < 2) return JSValue::undefined();
    std::string name = args[1].to_string();
    if (ctx->element->has_attribute(name)) {
        return JSValue::string(ctx->element->get_attribute(name));
    }
    return JSValue::null();
}

JSValue DOMBindings::native_set_attribute(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    // args[0] = this, args[1] = name, args[2] = value
    if (args.size() < 3) return JSValue::undefined();
    ctx->element->attributes[args[1].to_string()] = args[2].to_string();
    return JSValue::undefined();
}

JSValue DOMBindings::native_append_child(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<NativeCallContext *>(context);
    if (args.size() < 2)
        return JSValue::undefined();
    if (args[1].type != JSValue::Type::OBJECT)
        return JSValue::null();

    auto *child_wrapper = args[1].object_val;
    auto *child_node = ctx->bindings->get_node_from_wrapper(child_wrapper);
    if (!child_node)
        return JSValue::null();

    // Take ownership from the old parent; nodes created by createElement or
    // removed by removeChild live in the orphan store instead.
    std::unique_ptr<html::Node> owned = html::detach_from_parent(child_node);
    if (!owned) {
        auto &orphans = ctx->bindings->orphan_owned_nodes_;
        auto it = std::find_if(
            orphans.begin(), orphans.end(), [child_node](const auto &p) { return p.get() == child_node; });
        if (it == orphans.end())
            return JSValue::null();
        owned = std::move(*it);
        orphans.erase(it);
    }

    child_node->parent = ctx->element;
    if (!ctx->element->children.empty()) {
        html::Node *last = ctx->element->children.back().get();
        last->next_sibling = child_node;
        child_node->prev_sibling = last;
    }
    ctx->element->children.push_back(std::move(owned));

    return args[1];
}

JSValue DOMBindings::native_remove_child(const std::vector<JSValue> &args, void *context) {
    auto *ctx = static_cast<NativeCallContext *>(context);
    if (args.size() < 2 || args[1].type != JSValue::Type::OBJECT)
        return JSValue::null();
    auto *child_node = ctx->bindings->get_node_from_wrapper(args[1].object_val);
    if (!child_node || child_node->parent != ctx->element)
        return JSValue::null();

    auto owned = html::detach_from_parent(child_node);
    if (!owned)
        return JSValue::null();
    // Keep the subtree alive: the JS wrapper still references it.
    ctx->bindings->orphan_owned_nodes_.push_back(std::move(owned));
    return args[1];
}

// Matches any of a comma-separated selector list against `el`.
static bool element_matches_selector_list(const std::string &selector_text,
                                          html::Element *el,
                                          html::Node *root) {
    css::CssParser parser(selector_text);
    auto selectors = parser.parse_selectors(selector_text);
    if (selectors.empty())
        return false;
    for (const auto &sel : selectors) {
        if (css::matches_selector(sel, el, root))
            return true;
    }
    return false;
}

JSValue DOMBindings::native_query_selector(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    // args[0] = this, args[1] = selector
    if (args.size() < 2) return JSValue::null();
    std::string selector = args[1].to_string();
    if (selector.empty()) return JSValue::null();

    html::Element* found = nullptr;
    html::traverse_depth_first(ctx->element, [&](html::Node* node) {
        if (found || node == ctx->element || node->type != html::NodeType::ELEMENT)
            return;
        auto* el = static_cast<html::Element*>(node);
        if (element_matches_selector_list(selector, el, ctx->element))
            found = el;
    });
    if (found)
        return ctx->bindings->wrap_element(found, ctx->vm);
    return JSValue::null();
}

JSValue DOMBindings::native_query_selector_all(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    auto* vm = ctx->vm;
    auto* arr = vm->heap()->alloc_object();
    arr->obj.is_array = true;
    if (args.size() >= 2) {
        std::string selector = args[1].to_string();
        if (!selector.empty()) {
            html::traverse_depth_first(ctx->element, [&](html::Node* node) {
                if (node == ctx->element || node->type != html::NodeType::ELEMENT)
                    return;
                auto* el = static_cast<html::Element*>(node);
                if (element_matches_selector_list(selector, el, ctx->element)) {
                    arr->obj.array_elements.push_back(ctx->bindings->wrap_element(el, vm));
                }
            });
        }
    }
    return JSValue::object(&arr->obj);
}

JSValue DOMBindings::native_create_element(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    if (args.size() < 2)
        return JSValue::null();
    std::string tag = args[1].to_string();
    for (auto &c : tag)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (tag.empty())
        return JSValue::null();
    auto el = html::create_element(tag);
    html::Element* raw = el.get();
    ctx->bindings->orphan_owned_nodes_.push_back(std::move(el));
    return ctx->bindings->wrap_element(raw, ctx->vm);
}

JSValue DOMBindings::native_create_text_node(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    std::string data = args.size() >= 2 ? args[1].to_string() : "";
    auto text = html::create_text(data);
    html::Node* raw = text.get();
    ctx->bindings->orphan_owned_nodes_.push_back(std::move(text));
    return ctx->bindings->wrap_node(raw, ctx->vm);
}

JSValue DOMBindings::native_get_text_content(const std::vector<JSValue>& args, void* context) {
    (void)args;
    auto* ctx = static_cast<NativeCallContext*>(context);
    return JSValue::string(html::inner_text(ctx->element));
}

JSValue DOMBindings::native_set_text_content(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    std::string data = args.size() >= 2 ? args[1].to_string() : "";
    ctx->element->children.clear();
    html::append_child(ctx->element, html::create_text(data));
    return JSValue::undefined();
}

JSValue DOMBindings::native_add_event_listener(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    // args[0] = this, args[1] = type, args[2] = handler
    if (args.size() < 3) return JSValue::undefined();
    std::string event_type = args[1].to_string();
    JSValue handler = args[2];
    if (handler.type == JSValue::Type::FUNCTION) {
        ctx->bindings->add_event_listener(ctx->element, event_type, handler);
    }
    return JSValue::undefined();
}

JSValue DOMBindings::native_get_element_by_id(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    // args[0] = this, args[1] = id
    if (args.size() < 2) return JSValue::null();
    std::string id = args[1].to_string();
    if (id.empty()) return JSValue::null();

    html::Element* found = nullptr;
    html::traverse_depth_first(ctx->element, [&](html::Node* node) {
        if (!found && node->type == html::NodeType::ELEMENT) {
            auto* el = static_cast<html::Element*>(node);
            if (el->has_attribute("id") && el->get_attribute("id") == id) {
                found = el;
            }
        }
    });

    if (found) {
        return ctx->bindings->wrap_element(found, ctx->vm);
    }
    return JSValue::null();
}

} // namespace browser::js
