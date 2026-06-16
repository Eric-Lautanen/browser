#include "test_framework.hpp"
#include "utility.hpp"
#include "../js/dom_bindings.hpp"
#include "../js/vm.hpp"
#include "../js/gc.hpp"
#include "../html/dom.hpp"
#include "../html/traversal.hpp"

using namespace browser::js;
using namespace browser::html;

struct Fixture {
    std::unique_ptr<VM> vm;
    DOMBindings bindings;
    std::unique_ptr<Element> root;
    Element* root_ptr;

    Fixture() {
        vm = std::make_unique<VM>();
        vm->register_builtins();
        root = create_element("html");
        root_ptr = root.get();
    }
};

static std::unique_ptr<Element> make_div_with_text(const std::string& text) {
    auto div = create_element("div");
    auto txt = create_text(text);
    append_child(div.get(), std::move(txt));
    return div;
}

TEST(dom_wrap_element, {
    Fixture f;
    auto div = create_element("div");
    auto* div_ptr = div.get();

    JSValue wrapper = f.bindings.wrap_element(div_ptr, f.vm.get());
    ASSERT(wrapper.type == JSValue::Type::OBJECT);
    ASSERT(wrapper.object_val != nullptr);

    JSValue inner_html_prop = wrapper.object_val->get("getInnerHTML");
    ASSERT(inner_html_prop.type == JSValue::Type::FUNCTION);

    JSValue get_attr_prop = wrapper.object_val->get("getAttribute");
    ASSERT(get_attr_prop.type == JSValue::Type::FUNCTION);
})

TEST(dom_get_inner_html, {
    Fixture f;
    auto div = make_div_with_text("hello");
    auto* div_ptr = div.get();

    f.root_ptr->children.push_back(std::move(div));
    JSValue wrapper = f.bindings.wrap_element(div_ptr, f.vm.get());

    JSValue inner_html_fn = wrapper.object_val->get("getInnerHTML");
    ASSERT(inner_html_fn.type == JSValue::Type::FUNCTION);

    JSValue result = inner_html_fn.function_val->native_fn({}, inner_html_fn.function_val->native_context);
    ASSERT(result.type == JSValue::Type::STRING);
    ASSERT(result.string_val.find("hello") != std::string::npos);
})

TEST(dom_get_attribute, {
    Fixture f;
    auto div = create_element("div");
    auto* div_ptr = div.get();
    div_ptr->attributes["class"] = "foo";
    f.root_ptr->children.push_back(std::move(div));

    JSValue wrapper = f.bindings.wrap_element(div_ptr, f.vm.get());
    JSValue attr_fn = wrapper.object_val->get("getAttribute");
    ASSERT(attr_fn.type == JSValue::Type::FUNCTION);

    JSValue result = attr_fn.function_val->native_fn(
        {wrapper, JSValue::string("class")}, attr_fn.function_val->native_context);
    ASSERT(result.type == JSValue::Type::STRING);
    ASSERT_EQ(result.string_val, "foo");

    JSValue missing = attr_fn.function_val->native_fn(
        {wrapper, JSValue::string("nonexistent")}, attr_fn.function_val->native_context);
    ASSERT(missing.type == JSValue::Type::NULL_VAL);
})

TEST(dom_document_global, {
    Fixture f;
    f.bindings.register_dom_bindings(f.vm.get(), f.root_ptr);

    JSValue doc = f.vm->global_object()->get("document");
    ASSERT(doc.type == JSValue::Type::OBJECT);
    ASSERT(doc.object_val != nullptr);

    JSValue get_elem_fn = doc.object_val->get("getElementById");
    ASSERT(get_elem_fn.type == JSValue::Type::FUNCTION);

    JSValue win = f.vm->global_object()->get("window");
    ASSERT(win.type == JSValue::Type::OBJECT);

    JSValue win_doc = win.object_val->get("document");
    ASSERT(win_doc.type == JSValue::Type::OBJECT);
    ASSERT(win_doc.object_val == doc.object_val);
})

TEST(dom_gc_safety, {
    Fixture f;
    f.bindings.register_dom_bindings(f.vm.get(), f.root_ptr);

    auto div = create_element("div");
    auto* div_ptr = div.get();
    f.root_ptr->children.push_back(std::move(div));

    JSValue wrapper = f.bindings.wrap_element(div_ptr, f.vm.get());

    for (int i = 0; i < 5; i++) {
        f.vm->heap()->collect(f.vm->gc_roots());
    }

    JSObject* still_there = f.bindings.get_dom_wrapper(div_ptr);
    ASSERT(still_there != nullptr);
    ASSERT(still_there == wrapper.object_val);
})

TEST(dom_event_listener, {
    Fixture f;
    auto div = create_element("div");
    auto* div_ptr = div.get();

    bool handler_called = false;

    auto* handler_fn = f.vm->create_native_fn(
        [](const std::vector<JSValue>&, void* context) -> JSValue {
            *static_cast<bool*>(context) = true;
            return JSValue::undefined();
        }, false, &handler_called
    );

    f.bindings.add_event_listener(div_ptr, "click", JSValue::function(handler_fn));
    f.bindings.fire_event(div_ptr, "click", f.vm.get());

    ASSERT(handler_called);
})

TEST(dom_create_element, {
    Fixture f;
    f.bindings.register_dom_bindings(f.vm.get(), f.root_ptr);
    
    // Simulate creating an element via native function
    auto div = create_element("div");
    auto* div_ptr = div.get();
    div_ptr->attributes["id"] = "test-div";
    append_child(f.root_ptr, std::move(div));
    
    JSValue wrapper = f.bindings.wrap_element(div_ptr, f.vm.get());
    ASSERT(wrapper.type == JSValue::Type::OBJECT);
    
    // Test getAttribute (args[0]=this/receiver, args[1]=attribute name)
    JSValue get_attr_fn = wrapper.object_val->get("getAttribute");
    JSValue id_val = get_attr_fn.function_val->native_fn(
        {wrapper, JSValue::string("id")}, get_attr_fn.function_val->native_context);
    ASSERT(id_val.type == JSValue::Type::STRING);
    ASSERT_EQ(id_val.string_val, "test-div");
    
    // Test querySelector via document
    JSValue doc = f.vm->global_object()->get("document");
    ASSERT(doc.type == JSValue::Type::OBJECT);
})

TEST(dom_prototype_chain, {
    Fixture f;
    auto* proto = f.vm->heap()->alloc_object();
    proto->obj.set("customMethod", JSValue::string("works"));
    
    auto* obj = f.vm->heap()->alloc_object();
    obj->obj.prototype = JSValue::object(&proto->obj);
    
    JSValue result = obj->obj.get_property("customMethod");
    ASSERT(result.type == JSValue::Type::STRING);
    ASSERT_EQ(result.string_val, "works");
})
