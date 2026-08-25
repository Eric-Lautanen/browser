#include "../parser.hpp"

namespace browser::html {

    void Parser::handle_initial(const Token &token) {
        if (token.index() == 0) {  // DOCTYPE
            auto &dt = std::get<DoctypeToken>(token);
            insert_doctype(dt);
            if (dt.force_quirks || dt.name != "html") {
                document_->quirks_mode = true;
            }
            mode_ = InsertionMode::BEFORE_HTML;
            return;
        }
        if (token.index() == 3) {  // CHARACTER
            char32_t c = std::get<CharacterToken>(token).character;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r')
                return;
            // Non-whitespace text before <!html>: parse error per spec — set quirks,
            // switch to before-html and reprocess.
            document_->quirks_mode = true;
        }
        if (token.index() == 2) {  // COMMENT
            insert_comment(std::get<CommentToken>(token).data);
            return;
        }
        mode_ = InsertionMode::BEFORE_HTML;
        handle_token(token);
    }

}  // namespace browser::html
