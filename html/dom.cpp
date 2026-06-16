#include "dom.hpp"

namespace browser::html {

std::string Element::id() const {
    auto it = attributes.find("id");
    return it != attributes.end() ? it->second : "";
}

std::vector<std::string> Element::class_list() const {
    std::vector<std::string> result;
    auto it = attributes.find("class");
    if (it == attributes.end()) return result;
    std::string remaining = it->second;
    std::size_t pos = 0;
    while (pos < remaining.size()) {
        while (pos < remaining.size() && remaining[pos] == ' ') pos++;
        if (pos >= remaining.size()) break;
        std::size_t end = remaining.find(' ', pos);
        if (end == std::string::npos) end = remaining.size();
        result.push_back(remaining.substr(pos, end - pos));
        pos = end + 1;
    }
    return result;
}

bool Element::has_attribute(const std::string& name) const {
    return attributes.find(name) != attributes.end();
}

std::string Element::get_attribute(const std::string& name) const {
    auto it = attributes.find(name);
    return it != attributes.end() ? it->second : "";
}

std::unique_ptr<Element> create_element(const std::string& tag_name) {
    return std::make_unique<Element>(tag_name);
}

std::unique_ptr<Text> create_text(const std::string& data) {
    auto t = std::make_unique<Text>();
    t->data = data;
    return t;
}

std::unique_ptr<Document> create_document() {
    return std::make_unique<Document>();
}

void append_child(Node* parent, std::unique_ptr<Node> child) {
    child->parent = parent;
    if (!parent->children.empty()) {
        Node* last = parent->children.back().get();
        last->next_sibling = child.get();
        child->prev_sibling = last;
    }
    parent->children.push_back(std::move(child));
}

} // namespace browser::html
