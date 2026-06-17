#include "traversal.hpp"
#include "parser.hpp"

namespace browser::html {

// NOTE: iterates parent->children, so `parent` itself is never checked.
// If parent is a Document this is fine (a Document has no tag_name).
// If parent is an Element, the caller must separately check the parent
// before calling if they want to match the root node.
Element* find_element_by_tag(Node* parent, const std::string& tag) {
    if (!parent) return nullptr;
    for (auto& child : parent->children) {
        if (child->type == NodeType::ELEMENT) {
            auto* el = static_cast<Element*>(child.get());
            if (el->tag_name == tag) return el;
            auto* found = find_element_by_tag(el, tag);
            if (found) return found;
        }
    }
    return nullptr;
}

Element* find_element_by_id(Node* parent, const std::string& id) {
    if (!parent) return nullptr;
    for (auto& child : parent->children) {
        if (child->type == NodeType::ELEMENT) {
            auto* el = static_cast<Element*>(child.get());
            if (el->get_attribute("id") == id || el->id() == id) return el;
            auto* found = find_element_by_id(el, id);
            if (found) return found;
        }
    }
    return nullptr;
}

Element* find_element_by_tag_shallow(Element* parent, const std::string& tag) {
    if (!parent) return nullptr;
    for (auto& child : parent->children) {
        if (child->type == NodeType::ELEMENT) {
            auto* el = static_cast<Element*>(child.get());
            if (el->tag_name == tag) return el;
        }
    }
    return nullptr;
}

static bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

static std::string normalize_whitespace(const std::string& s) {
    std::string result;
    bool last_was_space = false;
    for (char c : s) {
        if (is_whitespace(c)) {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }
    // Trim trailing space
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

std::string inner_text(Element* element) {
    if (!element) return "";
    std::string result;
    bool first_block = true;
    for (auto& child : element->children) {
        if (child->type == NodeType::TEXT) {
            std::string text = static_cast<Text*>(child.get())->data;
            result += normalize_whitespace(text);
        } else if (child->type == NodeType::ELEMENT) {
            auto* el = static_cast<Element*>(child.get());
            if (el->tag_name == "br") {
                result += '\n';
            } else if (el->tag_name == "p" || el->tag_name == "div" || el->tag_name == "h1" ||
                       el->tag_name == "h2" || el->tag_name == "h3" || el->tag_name == "h4" ||
                       el->tag_name == "h5" || el->tag_name == "h6" || el->tag_name == "li" ||
                       el->tag_name == "tr" || el->tag_name == "td" || el->tag_name == "th") {
                if (!first_block && !result.empty() && result.back() != '\n') {
                    result += '\n';
                }
                result += inner_text(el);
                if (!result.empty() && result.back() != '\n') {
                    result += '\n';
                }
                first_block = false;
            } else {
                result += inner_text(el);
            }
        }
    }
    return result;
}

std::string serialize_dom(Node* node) {
    if (!node) return "";
    std::string result;
    switch (node->type) {
        case NodeType::DOCUMENT:
            for (auto& child : node->children) {
                result += serialize_dom(child.get());
            }
            break;
        case NodeType::ELEMENT: {
            auto* el = static_cast<Element*>(node);
            result += "<" + el->tag_name;
            for (const auto& [key, val] : el->attributes) {
                std::string escaped;
                for (char c : val) {
                    if (c == '&') escaped += "&amp;";
                    else if (c == '"') escaped += "&quot;";
                    else if (c == '<') escaped += "&lt;";
                    else if (c == '>') escaped += "&gt;";
                    else escaped += c;
                }
                result += " " + key + "=\"" + escaped + "\"";
            }
            result += ">";
            bool is_void = is_void_element(el->tag_name);
            if (!is_void) {
                for (auto& child : node->children) {
                    result += serialize_dom(child.get());
                }
                result += "</" + el->tag_name + ">";
            }
            break;
        }
        case NodeType::TEXT:
            result += static_cast<Text*>(node)->data;
            break;
        case NodeType::COMMENT:
            result += "<!--" + static_cast<Comment*>(node)->data + "-->";
            break;
        case NodeType::DOCUMENT_TYPE:
            result += "<!DOCTYPE";
            {
                auto* dt = static_cast<DocumentType*>(node);
                if (!dt->name.empty()) result += " " + dt->name;
                if (!dt->public_id.empty()) result += " PUBLIC \"" + dt->public_id + "\"";
                if (!dt->system_id.empty()) result += " \"" + dt->system_id + "\"";
            }
            result += ">";
            break;
    }
    return result;
}

} // namespace browser::html
