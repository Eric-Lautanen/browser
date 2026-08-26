#pragma once
#include "dom.hpp"

#include <string>
#include <vector>

namespace browser::html {

    // X-C2: explicit-stack pre-order traversal. The recursive version overflowed
    // the stack on deep attacker-shaped trees (a page is capped at 512 nesting
    // levels, but serialized/legacy documents and other callers are not).
    template <typename Fn>
    void traverse_depth_first(Node *node, Fn &&callback) {
        if (!node)
            return;
        std::vector<Node *> stack;
        stack.reserve(64);
        stack.push_back(node);
        while (!stack.empty()) {
            Node *current = stack.back();
            stack.pop_back();
            callback(current);
            // Push children in reverse so the first child is visited first.
            for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
                if (it->get())
                    stack.push_back(it->get());
            }
        }
}
Element* find_element_by_tag(Node* parent, const std::string& tag);
Element* find_element_by_tag_shallow(Element* parent, const std::string& tag);
Element* find_element_by_id(Node* parent, const std::string& id);
std::string inner_text(Element* element);
std::string serialize_dom(Node* node);

} // namespace browser::html
