#include "../parser.hpp"
#include <unordered_set>

namespace browser::html {

void Parser::adoption_agency_algorithm(const std::string& subject) {
    if (active_formatting_elements_.empty()) {
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] && stack_[static_cast<u32>(i)]->tag_name == subject) {
                generate_implied_end_tags({subject});
                stack_.resize(static_cast<u32>(i));
                return;
            }
        }
        return;
    }

    // Outer loop: max 8 iterations per spec
    static const std::unordered_set<std::string> special_tags = {
        "address", "blockquote", "center", "dir", "div", "dl", "fieldset",
        "figure", "figcaption", "footer", "header", "hgroup", "main", "nav",
        "ol", "p", "section", "ul", "pre", "listing", "form", "table",
        "table-row", "table-cell", "table-body", "table-header", "table-footer",
        "hr", "li", "dd", "dt", "h1", "h2", "h3", "h4", "h5", "h6"
    };

    for (int outer = 0; outer < 8; outer++) {
        // Step 1: Find the formatting element (last in list matching subject)
        Element* formatting_element = nullptr;
        int formatting_index = -1;
        for (i32 i = static_cast<i32>(active_formatting_elements_.size()) - 1; i >= 0; i--) {
            if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) break;
            if (active_formatting_elements_[static_cast<u32>(i)] &&
                active_formatting_elements_[static_cast<u32>(i)]->tag_name == subject) {
                formatting_element = active_formatting_elements_[static_cast<u32>(i)];
                formatting_index = i;
                break;
            }
        }

        // Step 2: If no formatting element, return
        if (!formatting_element) return;

        // Step 3: Find formatting element in stack of open elements
        int formatting_stack_index = -1;
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] == formatting_element) {
                formatting_stack_index = i;
                break;
            }
        }

        // Step 4: If not in stack, remove from list and return
        if (formatting_stack_index < 0) {
            active_formatting_elements_.erase(
                active_formatting_elements_.begin() + formatting_index);
            return;
        }

        // Step 5: Check if formatting element is in scope
        if (!has_element_in_scope(subject)) {
            return;
        }

        // Step 6: Find furthest block (first special tag after formatting element)
        int furthest_block = -1;
        for (i32 i = formatting_stack_index + 1; i < static_cast<i32>(stack_.size()); i++) {
            if (stack_[static_cast<u32>(i)] &&
                special_tags.find(stack_[static_cast<u32>(i)]->tag_name) != special_tags.end()) {
                furthest_block = i;
                break;
            }
        }

        // Step 7: No furthest block — pop to formatting element and remove from list
        if (furthest_block < 0) {
            generate_implied_end_tags();
            for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
                if (stack_[static_cast<u32>(i)] && stack_[static_cast<u32>(i)]->tag_name == subject) {
                    stack_.resize(static_cast<u32>(i));
                    break;
                }
            }
            active_formatting_elements_.erase(
                active_formatting_elements_.begin() + formatting_index);
            return;
        }

        // Steps 8-19: Simplified bookkeeping
        // Generate implied end tags (except subject)
        generate_implied_end_tags({subject});

        // Clone the formatting element and add to stack after furthest block
        TagToken clone_tok;
        clone_tok.type = TokenType::START_TAG;
        clone_tok.tag_name = formatting_element->tag_name;
        for (const auto& [k, v] : formatting_element->attributes) {
            Attribute attr;
            attr.name = k;
            attr.value = v;
            clone_tok.attributes.push_back(attr);
        }
        auto* formatting_clone = create_element_for_token(clone_tok);

        // Remove formatting element from active list
        active_formatting_elements_.erase(
            active_formatting_elements_.begin() + formatting_index);

        // Pop stack up to and including the formatting element
        stack_.resize(static_cast<u32>(formatting_stack_index));

        // Insert the clone after the furthest block
        Node* fb_node = stack_[static_cast<u32>(furthest_block)];
        if (fb_node && fb_node->parent) {
            auto& fb_siblings = fb_node->parent->children;
            for (auto it = fb_siblings.begin(); it != fb_siblings.end(); ++it) {
                if (it->get() == fb_node) {
                    formatting_clone->parent = fb_node->parent;
                    fb_siblings.insert(it + 1,
                        std::unique_ptr<Node>(static_cast<Node*>(formatting_clone)));
                    break;
                }
            }
        }

        // Add clone to stack after furthest block and to active list
        stack_.insert(
            stack_.begin() + furthest_block + 1,
            formatting_clone);
        active_formatting_elements_.push_back(formatting_clone);
    }
}

void Parser::push_active_formatting_element(Element* el) {
    int count = 0;
    for (i32 i = static_cast<i32>(active_formatting_elements_.size()) - 1; i >= 0; i--) {
        if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) break;
        if (active_formatting_elements_[static_cast<u32>(i)] == el ||
            (active_formatting_elements_[static_cast<u32>(i)] &&
             active_formatting_elements_[static_cast<u32>(i)]->tag_name == el->tag_name)) {
            count++;
        }
        if (count >= 3) {
            active_formatting_elements_.erase(
                active_formatting_elements_.begin() + static_cast<i64>(i));
            break;
        }
    }
    active_formatting_elements_.push_back(el);
}

void Parser::remove_active_formatting_element(Element* el) {
    for (auto it = active_formatting_elements_.begin(); it != active_formatting_elements_.end(); ++it) {
        if (*it == el) {
            active_formatting_elements_.erase(it);
            return;
        }
    }
}

int Parser::position_in_active_formatting_list(Element* el) {
    for (u32 i = 0; i < active_formatting_elements_.size(); i++) {
        if (active_formatting_elements_[i] == el) return static_cast<i32>(i);
    }
    return -1;
}

void Parser::reconstruct_active_formatting_elements() {
    if (active_formatting_elements_.empty()) return;

    i32 entry = static_cast<i32>(active_formatting_elements_.size()) - 1;
    if (entry < 0) return;

    if (active_formatting_elements_[static_cast<u32>(entry)] == nullptr) return;

    if (entry >= 0 && active_formatting_elements_[static_cast<u32>(entry)]) {
        Element* el = active_formatting_elements_[static_cast<u32>(entry)];
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] == el) return;
        }
    }

    i32 pos = -1;
    for (i32 i = entry; i >= 0; i--) {
        if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) {
            pos = i + 1;
            break;
        }
        Element* e = active_formatting_elements_[static_cast<u32>(i)];
        bool found = false;
        for (u32 j = 0; j < stack_.size(); j++) {
            if (stack_[j] == e) { found = true; break; }
        }
        if (found) {
            pos = i;
            break;
        }
        pos = i;
    }
    if (pos < 0) return;

    for (i32 i = pos; i <= entry; i++) {
        Element* e = active_formatting_elements_[static_cast<u32>(i)];
        if (e == nullptr) continue;

        TagToken clone_tok;
        clone_tok.type = TokenType::START_TAG;
        clone_tok.tag_name = e->tag_name;
        auto* clone = create_element_for_token(clone_tok);
        for (const auto& [k, v] : e->attributes) {
            clone->attributes[k] = v;
        }
        insert_element(clone);
        stack_.push_back(clone);
        active_formatting_elements_[static_cast<u32>(i)] = clone;
    }
}

} // namespace browser::html
