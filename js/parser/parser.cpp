#include "parser.hpp"
#include <sstream>

namespace browser::js {

// ---------------------------------------------------------------------------
// Construction and helpers
// ---------------------------------------------------------------------------

Parser::Parser(const std::string& source)
    : lexer_(source) {
    advance();
}

void Parser::advance() {
    current_ = lexer_.next();
}

void Parser::expect(TokenType type) {
    if (current_.type != type) {
        std::ostringstream oss;
        oss << "expected token " << static_cast<int>(type)
            << " but got '" << current_.text << "' at line " << current_.line;
        error(oss.str());
    } else {
        advance();
    }
}

bool Parser::match(TokenType type) {
    if (current_.type == type) {
        advance();
        return true;
    }
    return false;
}

void Parser::error(const std::string& msg) {
    errors_.push_back(msg);
}

void Parser::synchronize() {
    int depth = 0;
    while (current_.type != TokenType::EOF_TOKEN) {
        if (current_.type == TokenType::SEMICOLON) {
            advance();
            return;
        }
        if (current_.type == TokenType::LBRACE) {
            depth++;
        }
        if (current_.type == TokenType::RBRACE) {
            if (depth > 0) {
                depth--;
            } else {
                return;
            }
        }
        if (depth == 0 && current_.type == TokenType::IDENTIFIER) {
            const auto& t = current_.text;
            if (t == "function" || t == "if" || t == "while" || t == "for" ||
                t == "var" || t == "let" || t == "const" || t == "return" ||
                t == "break" || t == "throw" || t == "try" || t == "catch" ||
                t == "finally") {
                return;
            }
        }
        advance();
    }
}

// ---------------------------------------------------------------------------
// Program
// ---------------------------------------------------------------------------

std::unique_ptr<Program> Parser::parse_program() {
    auto prog = std::make_unique<Program>();
    prog->line = 1;
    while (current_.type != TokenType::EOF_TOKEN) {
        auto stmt = parse_statement();
        if (stmt) {
            prog->body.push_back(std::move(stmt));
        }
        // parse_statement already advances past semicolons and synchronizes;
        // no extra advance needed here.
    }
    return prog;
}

} // namespace browser::js
