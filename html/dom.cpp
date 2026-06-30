#include "dom.hpp"

namespace browser::html {

    std::string Element::id() const {
        auto it = attributes.find("id");
        return it != attributes.end() ? it->second : "";
    }

    std::vector<std::string> Element::class_list() const {
        std::vector<std::string> result;
        auto it = attributes.find("class");
        if (it == attributes.end())
            return result;
        const std::string &remaining = it->second;
        std::size_t pos = 0;
        while (pos < remaining.size()) {
            while (pos < remaining.size() && (remaining[pos] == ' ' || remaining[pos] == '\t' ||
                   remaining[pos] == '\n' || remaining[pos] == '\f' || remaining[pos] == '\r'))
                pos++;
            if (pos >= remaining.size())
                break;
            std::size_t end = pos;
            while (end < remaining.size() && remaining[end] != ' ' && remaining[end] != '\t' &&
                   remaining[end] != '\n' && remaining[end] != '\f' && remaining[end] != '\r')
                end++;
            result.push_back(remaining.substr(pos, end - pos));
            pos = end;
        }
        return result;
    }

    bool Element::has_attribute(const std::string &name) const {
        return attributes.find(name) != attributes.end();
    }

    std::string Element::get_attribute(const std::string &name) const {
        auto it = attributes.find(name);
        return it != attributes.end() ? it->second : "";
    }

    std::unique_ptr<Element> create_element(const std::string &tag_name) {
        return std::make_unique<Element>(tag_name);
    }

    std::unique_ptr<Text> create_text(const std::string &data) {
        auto t = std::make_unique<Text>();
        t->data = data;
        return t;
    }

    std::unique_ptr<Document> create_document() {
        return std::make_unique<Document>();
    }

    void append_child(Node *parent, std::unique_ptr<Node> child) {
        child->parent = parent;
        if (!parent->children.empty()) {
            Node *last = parent->children.back().get();
            last->next_sibling = child.get();
            child->prev_sibling = last;
        }
        parent->children.push_back(std::move(child));
    }

    void insert_before(Node *parent, std::unique_ptr<Node> child, Node *ref) {
        child->parent = parent;

        if (!ref) {
            // Insert before the last child (i.e. new child becomes second-to-last)
            if (parent->children.empty()) {
                child->next_sibling = nullptr;
                child->prev_sibling = nullptr;
                parent->children.push_back(std::move(child));
                return;
            }
            Node *last = parent->children.back().get();
            child->prev_sibling = last->prev_sibling;
            child->next_sibling = last;
            if (last->prev_sibling)
                last->prev_sibling->next_sibling = child.get();
            last->prev_sibling = child.get();
            auto it = parent->children.end();
            parent->children.insert(it - 1, std::move(child));
            return;
        }

        child->next_sibling = ref;
        child->prev_sibling = ref->prev_sibling;
        if (child->prev_sibling)
            child->prev_sibling->next_sibling = child.get();
        ref->prev_sibling = child.get();
        for (auto it = parent->children.begin(); it != parent->children.end(); ++it) {
            if (it->get() == ref) {
                parent->children.insert(it, std::move(child));
                return;
            }
        }
        parent->children.push_back(std::move(child));
    }

}  // namespace browser::html
