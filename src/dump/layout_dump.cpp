#include "layout_dump.hpp"
#include "json_writer.hpp"
#include "../../core/utility.hpp"
#include "../../css/layout.hpp"
// ---------------------------------------------------------------------------
// Layout dump
// ---------------------------------------------------------------------------
std::string edge_json(const browser::css::EdgeSizes &e) {
    json::Obj r;
    r.kv_num("top", e.top);
    r.kv_num("right", e.right);
    r.kv_num("bottom", e.bottom);
    r.kv_num("left", e.left);
    return r.done();
}

std::string dump_layout_node(const browser::css::LayoutNode *node) {
    if (!node)
        return "null";
    json::Obj o;
    if (!node->is_text()) {
        auto *n = node->node();
        if (n && n->type == browser::html::NodeType::ELEMENT)
            o.kv_raw("tag", json::q(static_cast<browser::html::Element *>(n)->tag_name));
        else
            o.kv("tag", "(anonymous)");
    } else {
        o.kv("tag", "(text)");
    }
    o.kv_bool("is_text", node->is_text());
    o.kv_raw("text", json::q(node->text()));
    json::Obj cr;
    cr.kv_num("x", node->content.x);
    cr.kv_num("y", node->content.y);
    cr.kv_num("width", node->content.width);
    cr.kv_num("height", node->content.height);
    o.kv_raw("content", cr.done());
    o.kv_raw("margin", edge_json(node->margin));
    o.kv_raw("padding", edge_json(node->padding));
    o.kv_raw("border", edge_json(node->border));
    if (!node->text_lines.empty()) {
        json::Arr lines;
        for (auto &li : node->text_lines) {
            json::Obj l;
            l.kv_num("y", li.y);
            l.kv_raw("text", json::q(li.text));
            lines.push(l.done());
        }
        o.kv_raw("text_lines", lines.done());
    }
    json::Arr kids;
    for (auto &ch : node->children) kids.push(dump_layout_node(ch.get()));
    o.kv_raw("children", kids.done());
    return o.done();
}

