#include "../parser.hpp"
#include "../dom.hpp"

namespace browser::html {

void Parser::handle_before_html(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') return;
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
            auto* html = create_element_for_token(tag);
            append_child(document_.get(), std::unique_ptr<Node>(html));
            stack_.push_back(html);
            mode_ = InsertionMode::BEFORE_HEAD;
            return;
        }
    }
    TagToken implied;
    implied.type = TokenType::START_TAG;
    implied.tag_name = "html";
    auto* html = create_element_for_token(implied);
    append_child(document_.get(), std::unique_ptr<Node>(html));
    stack_.push_back(html);
    mode_ = InsertionMode::BEFORE_HEAD;
    handle_token(token);
}

} // namespace browser::html
