#include "../dom.hpp"
#include "../parser.hpp"
#include "../utf8.hpp"

namespace browser::html {

    void Parser::insert_element(Element *element) {
        flush_pending_text();
        if (foster_parenting_) {
            for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
                if (stack_[i] && stack_[i]->tag_name == "table") {
                    Node *table_parent = stack_[i]->parent;
                    if (!table_parent)
                        break;
                    insert_before(table_parent, std::unique_ptr<Node>(static_cast<Node *>(element)), stack_[i]);
                    return;
                }
            }
            // No table found — fall back to appending to the document root
            if (current_node()) {
                append_child(current_node(), std::unique_ptr<Node>(static_cast<Node *>(element)));
                return;
            }
            if (document_) {
                element->parent = document_.get();
                document_->children.push_back(std::unique_ptr<Node>(static_cast<Node *>(element)));
                return;
            }
            return;  // Last resort: element leak rather than crash
        }
        if (current_node()) {
            append_child(current_node(), std::unique_ptr<Node>(static_cast<Node *>(element)));
        }
    }

    void Parser::flush_pending_text() {
        if (pending_text_.empty())
            return;
        if (foster_parenting_) {
            for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
                if (stack_[i] && stack_[i]->tag_name == "table") {
                    auto text_node = create_text(pending_text_);
                    pending_text_.clear();
                    Node *table_parent = stack_[i]->parent;
                    if (!table_parent)
                        break;
                    auto &siblings = table_parent->children;
                    auto it = siblings.begin();
                    for (; it != siblings.end(); ++it) {
                        if (it->get() == stack_[i])
                            break;
                    }
                    text_node->parent = table_parent;
                    siblings.insert(it, std::move(text_node));
                    return;
                }
            }
        }
        if (current_node()) {
            if (!current_node()->children.empty()) {
                auto &last = current_node()->children.back();
                if (last->type == NodeType::TEXT) {
                    auto *text_node = static_cast<Text *>(last.get());
                    text_node->data += pending_text_;
                    pending_text_.clear();
                    return;
                }
            }
            append_child(current_node(), create_text(pending_text_));
        }
        pending_text_.clear();
    }

}  // namespace browser::html
