#pragma once
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include "../tests/utility.hpp"
#include "value.hpp"
#include "../html/dom.hpp"

namespace browser::js {

class VM;
class GCJSObject;

struct DOMWrapperMap {
    std::unordered_map<html::Node*, GCJSObject*> map;
};

struct NativeCallContext {
    html::Element* element;
    class DOMBindings* bindings;
    VM* vm;
};

class DOMBindings;

// Hook for extending element bindings (set from higher-level code)
using ElementExtender = void (*)(DOMBindings* bindings, js::VM* vm, html::Element* element);

class DOMBindings {
public:
    DOMBindings();
    ~DOMBindings();

    static void set_element_extender(ElementExtender ext) { element_extender_ = ext; }

    JSValue wrap_element(html::Element* element, VM* vm);
    JSObject* get_dom_wrapper(html::Node* node) const;
    void register_dom_bindings(VM* vm, html::Element* document_element);

    struct ListenerEntry {
        std::string type;
        JSValue handler;
    };
    void add_event_listener(html::Node* node, const std::string& event_type, JSValue handler);
    std::vector<ListenerEntry> get_event_listeners(html::Node* node) const;
    void fire_event(html::Node* node, const std::string& event_type, VM* vm);

    std::vector<JSValue*> gc_roots();

private:
    DOMWrapperMap wrappers_;
    std::vector<JSValue> gc_stable_;
    std::unordered_map<html::Node*, std::vector<ListenerEntry>> event_listeners_;

    NativeCallContext* make_context(html::Element* el, VM* vm);
    std::vector<std::unique_ptr<NativeCallContext>> contexts_;

    void set_up_element_methods(JSObject* obj, html::Element* el, VM* vm);
    void set_up_document_methods(JSObject* obj, html::Element* el, VM* vm);

    std::shared_ptr<bool> gc_alive_ = std::make_shared<bool>(true);

    static JSValue native_get_inner_html(const std::vector<JSValue>& args, void* context);
    static JSValue native_get_attribute(const std::vector<JSValue>& args, void* context);
    static JSValue native_set_attribute(const std::vector<JSValue>& args, void* context);
    static JSValue native_append_child(const std::vector<JSValue>& args, void* context);
    static JSValue native_query_selector(const std::vector<JSValue>& args, void* context);
    static JSValue native_add_event_listener(const std::vector<JSValue>& args, void* context);
    static JSValue native_get_element_by_id(const std::vector<JSValue>& args, void* context);

    static ElementExtender element_extender_;
};

} // namespace browser::js
