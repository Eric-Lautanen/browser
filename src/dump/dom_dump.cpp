#include "dom_dump.hpp"
#include "json_writer.hpp"
#include "../../html/dom.hpp"
#include <algorithm>
#include <vector>

std::string dump_doctype(const browser::html::DocumentType *dt, int indent) {
    json::Obj o(indent);
    o.kv("type", "doctype");
    o.kv_raw("name", json::q(dt->name));
    o.kv_raw("public_id", json::q(dt->public_id));
    o.kv_raw("system_id", json::q(dt->system_id));
    return o.done();
}

std::string dump_node(const browser::html::Node *node, int indent) {
    if (!node) return "null";
    int ci = indent >= 0 ? indent + 1 : -1;
    if (node->type == browser::html::NodeType::ELEMENT) {
        auto *el = static_cast<const browser::html::Element *>(node);
        json::Obj o(indent);
        o.kv("type", "element");
        o.kv_raw("tag", json::q(el->tag_name));
        json::Obj attrs(ci);
        std::vector<std::string> attr_keys;
        for (auto &[k, v] : el->attributes) attr_keys.push_back(k);
        std::sort(attr_keys.begin(), attr_keys.end());
        for (auto &k : attr_keys) attrs.kv_raw(k, json::q(el->attributes.at(k)));
        o.kv_raw("attributes", attrs.done());
        json::Arr kids(ci);
        for (auto &ch : node->children) kids.push(dump_node(ch.get(), ci + 1));
        o.kv_raw("children", kids.done());
        return o.done();
    }
    if (node->type == browser::html::NodeType::TEXT) {
        auto *tx = static_cast<const browser::html::Text *>(node);
        json::Obj o(indent);
        o.kv("type", "text");
        o.kv_raw("data", json::q(tx->data));
        std::string normalized = tx->data;
        {
            std::string r; bool last_was_space = false;
            for (char c : normalized) {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    if (!last_was_space) { r += ' '; last_was_space = true; }
                } else { r += c; last_was_space = false; }
            }
            while (!r.empty() && r.back() == ' ') r.pop_back();
            while (!r.empty() && r.front() == ' ') r.erase(r.begin());
            normalized = r;
        }
        o.kv_raw("data_normalized", json::q(normalized));
        return o.done();
    }
    if (node->type == browser::html::NodeType::COMMENT) {
        auto *cm = static_cast<const browser::html::Comment *>(node);
        json::Obj o(indent);
        o.kv("type", "comment");
        o.kv_raw("data", json::q(cm->data));
        return o.done();
    }
    if (node->type == browser::html::NodeType::DOCUMENT_TYPE) {
        return dump_doctype(static_cast<const browser::html::DocumentType *>(node), indent);
    }
    return "null";
}

std::string dump_dom_document(const std::string &source, browser::html::Document *doc) {
    int ci = 1;
    json::Obj out(0);
    std::string norm_source; norm_source.reserve(source.size());
    for (char c : source) norm_source += (c == '\\' ? '/' : c);
    out.kv_raw("source", json::q(norm_source));
    std::string encoding = "UTF-8";
    for (auto &ch : doc->children) {
        if (ch->type == browser::html::NodeType::ELEMENT) {
            auto *el = static_cast<browser::html::Element *>(ch.get());
            if (el->tag_name == "html") {
                for (auto &html_ch : el->children) {
                    if (html_ch->type == browser::html::NodeType::ELEMENT) {
                        auto *head_el = static_cast<browser::html::Element *>(html_ch.get());
                        if (head_el->tag_name == "head") {
                            for (auto &head_ch : head_el->children) {
                                if (head_ch->type == browser::html::NodeType::ELEMENT) {
                                    auto *meta = static_cast<browser::html::Element *>(head_ch.get());
                                    if (meta->tag_name == "meta") {
                                        auto it = meta->attributes.find("charset");
                                        if (it != meta->attributes.end()) encoding = it->second;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    out.kv_raw("encoding", json::q(encoding));
    const browser::html::DocumentType *doctype_node = nullptr;
    for (auto &ch : doc->children) if (ch->type == browser::html::NodeType::DOCUMENT_TYPE) { doctype_node = static_cast<browser::html::DocumentType *>(ch.get()); break; }
    if (doctype_node) {
        json::Obj dt(ci);
        dt.kv_raw("name", json::q(doctype_node->name));
        dt.kv_raw("public_id", json::q(doctype_node->public_id));
        dt.kv_raw("system_id", json::q(doctype_node->system_id));
        out.kv_raw("doctype", dt.done());
    } else out.kv_raw("doctype", "null");
    json::Arr kids(ci);
    for (auto &ch : doc->children) kids.push(dump_node(ch.get(), ci + 1));
    out.kv_raw("children", kids.done());
    out.kv_raw("quirks_mode", doc->quirks_mode ? "true" : "false");
    return out.done();
}
