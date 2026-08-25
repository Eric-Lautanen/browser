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
        // Inside SVG/MathML, <script>/<style> content wrapped in a CDATA section
        // keeps its text but loses the <![CDATA[ / ]]> markers.
        if (foreign_) {
            Node *parent = current_node();
            if (parent && parent->type == NodeType::ELEMENT) {
                auto *pel = static_cast<Element *>(parent);
                if (pel->tag_name == "script" || pel->tag_name == "style") {
                    size_t begin = pending_text_.find("<![CDATA[");
                    if (begin != std::string::npos) {
                        size_t end = pending_text_.rfind("]]>");
                        if (end != std::string::npos && end > begin) {
                            pending_text_.erase(end, 3);
                            pending_text_.erase(begin, 9);
                        }
                    }
                }
            }
        }
        // Spec ("in table text"): non-whitespace characters collected directly
        // in a table context are foster-parented out of the table. Only applies
        // when the pending text's parent would be a table-structure element;
        // content whose parent is a fostered-out or regular element stays put.
        bool foster = foster_parenting_;
        if (!foster) {
            Node *parent = current_node();
            if (parent && parent->type == NodeType::ELEMENT) {
                auto *pel = static_cast<Element *>(parent);
                const std::string &ptag = pel->tag_name;
                bool table_context =
                    (ptag == "table" || ptag == "tbody" || ptag == "tfoot" || ptag == "thead" || ptag == "tr");
                if (table_context) {
                    for (char c : pending_text_) {
                        if (c != ' ' && c != '\t' && c != '\n' && c != '\f' && c != '\r') {
                            foster = true;
                            break;
                        }
                    }
                }
            }
        }
        if (foster) {
            for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
                if (stack_[i] && stack_[i]->tag_name == "table") {
                    Node *table_parent = stack_[i]->parent;
                    if (!table_parent)
                        break;
                    auto &siblings = table_parent->children;
                    auto it = siblings.begin();
                    for (; it != siblings.end(); ++it) {
                        if (it->get() == stack_[i])
                            break;
                    }
                    // Merge with an adjacent text node to keep runs contiguous.
                    if (it != siblings.begin()) {
                        Node *prev = (it - 1)->get();
                        if (prev && prev->type == NodeType::TEXT) {
                            static_cast<Text *>(prev)->data += pending_text_;
                            pending_text_.clear();
                            return;
                        }
                    }
                    auto text_node = create_text(pending_text_);
                    pending_text_.clear();
                    text_node->parent = table_parent;
                    siblings.insert(it, std::move(text_node));
                    return;
                }
            }
            // No table found — fall through to normal insertion below.
        }
        // Spec: unclosed formatting elements are re-created before text is
        // inserted, so misnested markup like <b><i>x</b>y</i> keeps "y" in a
        // reopened <i>.
        reconstruct_active_formatting_elements();
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
