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
        virtual ~Node() = default;
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

}  // namespace browser::html
