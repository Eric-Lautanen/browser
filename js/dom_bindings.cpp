#include "dom_bindings.hpp"
#include "vm.hpp"
#include "gc.hpp"
#include "../html/traversal.hpp"

namespace browser::js {

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
    obj->set("querySelector", JSValue::function(vm->create_native_fn(native_query_selector, false, ctx)));
    obj->set("addEventListener", JSValue::function(vm->create_native_fn(native_add_event_listener, false, ctx)));
}

void DOMBindings::set_up_document_methods(JSObject* obj, html::Element* el, VM* vm) {
    auto* ctx = make_context(el, vm);
    obj->set("getElementById", JSValue::function(vm->create_native_fn(native_get_element_by_id, false, ctx)));
}

JSValue DOMBindings::wrap_element(html::Element* element, VM* vm) {
    auto* existing = get_dom_wrapper(element);
    if (existing) return JSValue::object(existing);

    auto* gc_obj = vm->heap()->alloc_object();
    auto* obj = &gc_obj->obj;

    wrappers_.map[element] = gc_obj;

    set_up_element_methods(obj, element, vm);

    return JSValue::object(obj);
}

JSObject* DOMBindings::get_dom_wrapper(html::Node* node) const {
    auto it = wrappers_.map.find(node);
    if (it != wrappers_.map.end()) return &it->second->obj;
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
            auto* fn = handler.function_val;
            if (fn->native_fn) {
                fn->native_fn({}, fn->native_context);
            } else if (fn->bytecode) {
                auto saved = vm->save_state();
                vm->execute(fn->bytecode);
                vm->restore_state(std::move(saved));
            }
        }
    }
}

std::vector<JSValue*> DOMBindings::gc_roots() {
    gc_stable_.clear();

    size_t total = wrappers_.map.size();
    for (auto& [n, listeners] : event_listeners_) {
        total += listeners.size();
    }
    gc_stable_.reserve(total);

    std::vector<JSValue*> roots;
    roots.reserve(total);

    for (auto& [node, gc_obj] : wrappers_.map) {
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

    vm->add_gc_root_provider([this, alive = gc_alive_]() -> std::vector<JSValue*> {
        if (!*alive) return {};
        return gc_roots();
    });
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

JSValue DOMBindings::native_append_child(const std::vector<JSValue>&, void*) {
    return JSValue::undefined();
}

JSValue DOMBindings::native_query_selector(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<NativeCallContext*>(context);
    // args[0] = this, args[1] = selector
    if (args.size() < 2) return JSValue::null();
    std::string selector = args[1].to_string();
    auto* found = html::find_element_by_tag(ctx->element, selector);
    if (found) {
        return ctx->bindings->wrap_element(found, ctx->vm);
    }
    return JSValue::null();
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
