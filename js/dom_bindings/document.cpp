#include "../dom_bindings.hpp"
#include "../vm.hpp"
#include "../gc.hpp"
#include "../../html/dom.hpp"
#include "../../html/traversal.hpp"
#include "../../html/parser.hpp"

namespace browser::js::dom_bindings {

struct DocCtx {
    html::Element* element;
    DOMBindings* bindings;
    VM* vm;
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

void register_document_bindings(VM* vm, DOMBindings* bindings, html::Element* document_element) {
    auto* ctx = new DocCtx{document_element, bindings, vm};
    JSValue doc_wrapper = bindings->wrap_element(document_element, vm);
    auto* doc_obj = doc_wrapper.object_val;
    
    doc_obj->set("getElementById", JSValue::function(vm->create_native_fn(doc_get_element_by_id, false, ctx)));
    doc_obj->set("createElement", JSValue::function(vm->create_native_fn(doc_create_element, false, ctx)));
    doc_obj->set("createTextNode", JSValue::function(vm->create_native_fn(doc_create_text_node, false, ctx)));
    doc_obj->set("querySelector", JSValue::function(vm->create_native_fn(doc_query_selector, false, ctx)));
    doc_obj->set("write", JSValue::function(vm->create_native_fn(doc_write, false, ctx)));
    doc_obj->set("writeln", JSValue::function(vm->create_native_fn(doc_write, false, ctx)));
    doc_obj->set("title", JSValue::string(""));
    vm->global_object()->set("document", doc_wrapper);
}

} // namespace browser::js::dom_bindings
