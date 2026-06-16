#pragma once
#include <string>
#include "dom.hpp"

namespace browser::html {

template<typename Fn>
void traverse_depth_first(Node* node, Fn&& callback) {
    if (!node) return;
    callback(node);
    for (auto& child : node->children) {
        traverse_depth_first(child.get(), std::forward<Fn>(callback));
    }
}
Element* find_element_by_tag(Node* parent, const std::string& tag);
Element* find_element_by_tag_shallow(Element* parent, const std::string& tag);
std::string inner_text(Element* element);
std::string serialize_dom(Node* node);

} // namespace browser::html
