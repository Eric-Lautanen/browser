#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace browser::html {

    enum class NodeType { DOCUMENT, ELEMENT, TEXT, COMMENT, DOCUMENT_TYPE };

    struct Node {
        NodeType type;
        std::vector<std::unique_ptr<Node>> children;
        Node *parent = nullptr;
        Node *next_sibling = nullptr;
        Node *prev_sibling = nullptr;
        // H-C1: destruction must not recurse per tree level — a hostile page
        // with tens of thousands of nesting levels would overflow the stack
        // when the document is freed. Defined in dom.cpp (iterative).
        virtual ~Node();
    };

    struct Document : Node {
        Document() { type = NodeType::DOCUMENT; }
        std::string url;
        bool quirks_mode = false;
    };

    struct DocumentType : Node {
        DocumentType() { type = NodeType::DOCUMENT_TYPE; }
        std::string name, public_id, system_id;
        bool force_quirks = false;
    };

    struct Element : Node {
        Element(const std::string &tag) : tag_name(tag) { type = NodeType::ELEMENT; }
        std::string tag_name;
        std::unordered_map<std::string, std::string> attributes;
        std::string namespace_uri = "http://www.w3.org/1999/xhtml";
        // Resource loader annotation: the absolute URL a relative src/href
        // resolved to (decode/paint key). Not serialized; empty when unset.
        std::string resolved_src;
        // <template>: child nodes live in a separate content fragment
        // (mirrors the DOM .content DocumentFragment) so they are neither
        // rendered nor serialized as regular children.
        bool is_template = false;
        std::vector<std::unique_ptr<Node>> template_content;

        std::string id() const;
        std::vector<std::string> class_list() const;
        bool has_attribute(const std::string &name) const;
        std::string get_attribute(const std::string &name) const;
    };

    struct Text : Node {
        Text() { type = NodeType::TEXT; }
        std::string data;
    };

    struct Comment : Node {
        Comment() { type = NodeType::COMMENT; }
        std::string data;
    };

    std::unique_ptr<Element> create_element(const std::string &tag_name);
    std::unique_ptr<Text> create_text(const std::string &data);
    std::unique_ptr<Document> create_document();
    void append_child(Node *parent, std::unique_ptr<Node> child);
    void insert_before(Node *parent, std::unique_ptr<Node> child, Node *ref);
    // Detaches `node` from its parent and returns ownership of the subtree.
    // Returns nullptr when the node has no parent or is not present in the
    // parent's child list (ownership stays where it is).
    std::unique_ptr<Node> detach_from_parent(Node *node);

}  // namespace browser::html
