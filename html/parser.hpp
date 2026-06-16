#pragma once
#include <memory>
#include <vector>
#include <string>
#include "../async/task.hpp"
#include "../async/executor.hpp"
#include "dom.hpp"
#include "tokenizer.hpp"
#include "preload_scanner.hpp"

namespace browser::html {

enum class InsertionMode {
    INITIAL, BEFORE_HTML, BEFORE_HEAD, IN_HEAD, AFTER_HEAD, IN_BODY,
    TEXT, IN_TABLE, IN_TABLE_BODY, IN_ROW, IN_CELL,
    IN_SELECT, IN_SELECT_IN_TABLE, IN_CAPTION, IN_COLUMN_GROUP,
    IN_TEMPLATE, IN_FRAMESET,
    AFTER_BODY, AFTER_AFTER_BODY, AFTER_HEAD_FRAMESET,
    AFTER_FRAMESET, AFTER_AFTER_FRAMESET
};

class Parser {
public:
    Parser();
    Parser(PreloadScanner* scanner, const std::string& base_url = "");
    std::unique_ptr<Document> parse(const std::string& html);

private:
    PreloadScanner* preload_scanner_ = nullptr;
    std::string base_url_;
    std::unique_ptr<Tokenizer> tokenizer_;
    std::unique_ptr<Document> document_;
    InsertionMode mode_ = InsertionMode::INITIAL;
    InsertionMode original_mode_ = InsertionMode::INITIAL;
    std::vector<Element*> stack_;
    std::vector<InsertionMode> template_modes_;
    bool foster_parenting_ = false;
    bool scripting_ = true;
    bool frameset_ok_ = true;

    std::string pending_text_;
    std::vector<Element*> active_formatting_elements_;
    Element* head_element_pointer_ = nullptr;

    Element* current_node() const;
    void handle_token(const Token& token);
    void handle_initial(const Token& token);
    void handle_before_html(const Token& token);
    void handle_before_head(const Token& token);
    void handle_in_head(const Token& token);
    void handle_after_head(const Token& token);
    void handle_in_body(const Token& token);
    void handle_text(const Token& token);
    void handle_in_table(const Token& token);
    void handle_in_table_body(const Token& token);
    void handle_in_row(const Token& token);
    void handle_in_cell(const Token& token);
    void handle_in_select(const Token& token);
    void handle_in_caption(const Token& token);
    void handle_in_column_group(const Token& token);
    void handle_in_template(const Token& token);
    void handle_in_frameset(const Token& token);
    void handle_after_frameset(const Token& token);
    void handle_after_after_frameset(const Token& token);
    void handle_after_body(const Token& token);
    void handle_after_after_body(const Token& token);

    void insert_element(Element* element);
    void insert_character(char32_t c);
    void flush_pending_text();
    void insert_comment(const std::string& data);
    void insert_doctype(const DoctypeToken& token);
    Element* create_element_for_token(const TagToken& token);
    void generate_implied_end_tags(const std::vector<std::string>& exceptions = {});
    bool has_element_in_scope(const std::string& tag_name, const std::vector<std::string>& extras = {});
    bool has_element_in_scope(const std::vector<std::string>& tags);
    bool has_element_in_list_scope(const std::string& tag_name);
    bool has_element_in_button_scope(const std::string& tag_name);
    bool has_element_in_table_scope(const std::string& tag_name);
    bool has_element_in_select_scope(const std::string& tag_name);
    void close_element(const std::string& tag_name);
    void close_cell();
    void clear_stack_back_to_table_context();
    void clear_stack_back_to_table_body_context();
    void clear_stack_back_to_table_row_context();
    void reconstruct_active_formatting_elements();
    void push_active_formatting_element(Element* el);
    void remove_active_formatting_element(Element* el);
    int position_in_active_formatting_list(Element* el);
    void adoption_agency_algorithm(const std::string& subject);

    static bool is_special_tag(const std::string& tag);
    static bool is_heading_tag(const std::string& tag);

    void parse_generic_start_tag(const TagToken& token);
    void parse_generic_end_tag(const TagToken& token);
    void reset_insertion_mode();
};

bool is_void_element(const std::string& tag);

inline async::task<std::unique_ptr<Document>> parse(const std::string& html) {
    co_await async::thread_pool_executor{};
    Parser p;
    co_return p.parse(html);
}

inline async::task<std::unique_ptr<Document>> parse(const std::string& html, PreloadScanner* scanner, const std::string& base_url) {
    co_await async::thread_pool_executor{};
    Parser p(scanner, base_url);
    co_return p.parse(html);
}

} // namespace browser::html
