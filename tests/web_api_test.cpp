#include "test_framework.hpp"
#include "utility.hpp"
#include "../js/ast.hpp"
#include "../js/parser.hpp"
#include "../js/compiler.hpp"
#include "../js/vm.hpp"
#include "../js/gc.hpp"
#include "../js/dom_bindings.hpp"
#include "../html/dom.hpp"
#include "../html/traversal.hpp"

using namespace browser;
using namespace browser::js;
using namespace browser::html;

struct WebFixture {
    std::unique_ptr<VM> vm;
    DOMBindings bindings;
    std::unique_ptr<Element> doc_el;
    Element* doc_ptr;

    WebFixture() {
        vm = std::make_unique<VM>();
        vm->register_builtins();
        doc_el = create_element("html");
        doc_ptr = doc_el.get();
        auto body = create_element("body");
        append_child(doc_ptr, std::move(body));
        bindings.register_dom_bindings(vm.get(), doc_ptr);
    }

    JSValue eval(const std::string& source) {
        Parser parser(source);
        auto program = parser.parse_program();
        if (!parser.errors().empty()) return JSValue::undefined();
        Compiler compiler;
        auto bc = compiler.compile(*program);
        if (!bc) return JSValue::undefined();
        return vm->execute(bc.get());
    }
};

TEST(web_script_getElementById, {
    WebFixture f;
    // Create a div with id
    auto div = create_element("div");
    auto* div_ptr = div.get();
    div_ptr->attributes["id"] = "test";
    auto txt = create_text("hello");
    append_child(div_ptr, std::move(txt));
    append_child(f.doc_ptr, std::move(div));
    
    JSValue result = f.eval("document.getElementById('test').getInnerHTML();");
    ASSERT(result.type == JSValue::Type::STRING);
})

TEST(web_script_innerHTML, {
    WebFixture f;
    auto div = create_element("div");
    auto* div_ptr = div.get();
    div_ptr->attributes["id"] = "content";
    auto txt = create_text("world");
    append_child(div_ptr, std::move(txt));
    append_child(f.doc_ptr, std::move(div));
    
    JSValue result = f.eval("document.getElementById('content').getInnerHTML();");
    ASSERT(result.type == JSValue::Type::STRING);
    ASSERT(result.string_val.find("world") != std::string::npos);
})

TEST(web_console_log, {
    WebFixture f;
    // Just verify it doesn't crash
    JSValue result = f.eval("console.log('test');");
    ASSERT(result.type == JSValue::Type::UNDEFINED);
})

TEST(web_prototype_chain, {
    WebFixture f;
    // Simulate prototype chain via JS objects by creating a constructor pattern
    // The JS engine's new operator creates proper prototype chains
    auto* proto = f.vm->heap()->alloc_object();
    proto->obj.set("value", JSValue::number(42));
    auto* child = f.vm->heap()->alloc_object();
    child->obj.prototype = JSValue::object(&proto->obj);
    child->obj.set("ownValue", JSValue::number(7));
    
    JSValue proto_val = child->obj.get_property("value");
    ASSERT(proto_val.type == JSValue::Type::NUMBER);
    ASSERT_EQ(proto_val.number_val, 42);
    
    JSValue own_val = child->obj.get_property("ownValue");
    ASSERT(own_val.type == JSValue::Type::NUMBER);
    ASSERT_EQ(own_val.number_val, 7);
})

TEST(web_parseInt, {
    WebFixture f;
    JSValue r = f.eval("parseInt('42');");
    ASSERT(r.type == JSValue::Type::NUMBER);
    ASSERT_EQ(r.number_val, 42);
})

TEST(web_math, {
    WebFixture f;
    JSValue r = f.eval("Math.abs(-5) + Math.ceil(1.2) + Math.floor(1.9) + Math.round(2.5);");
    ASSERT(r.type == JSValue::Type::NUMBER);
    ASSERT(r.number_val > 9.0);
})
