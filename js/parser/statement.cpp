#include "parser.hpp"
#include <sstream>

namespace browser::js {

// ---------------------------------------------------------------------------
// Statement parsing
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_statement() {
    if (current_.type == TokenType::LBRACE) {
        return parse_block();
    }
    if (current_.type == TokenType::SEMICOLON) {
        advance();
        return std::make_unique<Stmt>(EmptyStmt{});
    }

    if (current_.type == TokenType::IDENTIFIER) {
        const auto& kw = current_.text;
        if (kw == "var" || kw == "let" || kw == "const") {
            auto stmt = parse_var_declaration();
            if (current_.type == TokenType::SEMICOLON) {
                advance();
            } else if (current_.type != TokenType::RBRACE && current_.type != TokenType::EOF_TOKEN) {
                error("expected ';' after variable declaration");
                synchronize();
            }
            return stmt;
        }
        if (kw == "function") {
            return parse_function_declaration();
        }
        if (kw == "if") {
            return parse_if();
        }
        if (kw == "while") {
            return parse_while();
        }
        if (kw == "for") {
            return parse_for();
        }
        if (kw == "return") {
            return parse_return();
        }
        if (kw == "break") {
            return parse_break();
        }
        if (kw == "throw") {
            return parse_throw();
        }
        if (kw == "try") {
            return parse_try();
        }
    }

    u32 expr_line = current_.line;
    auto expr = parse_expression(0);
    if (!expr) {
        // parse_expression already emitted an error for the unexpected token
        return nullptr;
    }

    if (current_.type == TokenType::SEMICOLON) {
        advance();
    } else if (current_.type == TokenType::RBRACE || current_.type == TokenType::EOF_TOKEN) {
        // ASI
    } else {
        std::ostringstream oss;
        oss << "expected semicolon after expression at line " << current_.line;
        error(oss.str());
        synchronize();
    }

    auto stmt = std::make_unique<ExpressionStmt>();
    stmt->line = expr_line;
    stmt->expr = std::move(expr);
    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// Block
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_block() {
    auto block = std::make_unique<BlockStmt>();
    block->line = current_.line;
    block->column = current_.column;
    expect(TokenType::LBRACE);
    brace_depth_++;

    while (current_.type != TokenType::RBRACE && current_.type != TokenType::EOF_TOKEN) {
        auto stmt = parse_statement();
        if (stmt) {
            block->body.push_back(std::move(stmt));
        }
    }

    brace_depth_--;
    if (current_.type == TokenType::RBRACE) {
        advance();
    } else {
        error("expected '}' at end of block");
    }
    return std::make_unique<Stmt>(std::move(*block));
}

// ---------------------------------------------------------------------------
// Variable declaration
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_var_declaration() {
    auto stmt = std::make_unique<VarDeclStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;

    if (current_.text == "var") stmt->kind = VarDeclStmt::Kind::VAR;
    else if (current_.text == "let") stmt->kind = VarDeclStmt::Kind::LET;
    else stmt->kind = VarDeclStmt::Kind::CONST;
    advance();

    for (;;) {
        VarDeclStmt::Declarator decl;

        decl.id = parse_pattern();
        if (!decl.id) {
            error("expected pattern in variable declaration");
            break;
        }

        if (match(TokenType::EQUALS)) {
            decl.init = parse_expression(0);
        } else if (stmt->kind == VarDeclStmt::Kind::CONST) {
            error("const declaration must have an initializer");
        }

        stmt->declarations.push_back(std::move(decl));

        if (!match(TokenType::COMMA)) {
            break;
        }
    }

    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// Function declaration
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_function_declaration() {
    auto stmt = std::make_unique<FuncDeclStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;
    advance(); // consume 'function'

    if (current_.type == TokenType::IDENTIFIER) {
        stmt->name = current_.text;
        advance();
    } else {
        error("expected function name");
    }

    expect(TokenType::LPAREN);
    while (current_.type != TokenType::RPAREN && current_.type != TokenType::EOF_TOKEN) {
        auto param = parse_pattern();
        if (param) {
            stmt->params.push_back(std::move(param));
        }
        if (!match(TokenType::COMMA)) {
            break;
        }
    }
    expect(TokenType::RPAREN);

    // Reuse parse_block for the function body
    auto body_stmt = parse_block();
    stmt->body = std::make_unique<BlockStmt>();
    auto& body_block = std::get<BlockStmt>(*body_stmt);
    stmt->body->body = std::move(body_block.body);

    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// If
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_if() {
    auto stmt = std::make_unique<IfStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;
    advance(); // consume 'if'

    expect(TokenType::LPAREN);
    stmt->test = parse_expression(0);
    expect(TokenType::RPAREN);

    stmt->consequent = parse_statement();

    if (current_.type == TokenType::IDENTIFIER && current_.text == "else") {
        advance();
        stmt->alternate = parse_statement();
    }

    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// While
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_while() {
    auto stmt = std::make_unique<WhileStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;
    advance(); // consume 'while'

    expect(TokenType::LPAREN);
    stmt->test = parse_expression(0);
    expect(TokenType::RPAREN);

    stmt->body = parse_statement();

    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// For
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_for() {
    auto stmt = std::make_unique<ForStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;
    advance(); // consume 'for'

    expect(TokenType::LPAREN);

    if (current_.type == TokenType::IDENTIFIER &&
        (current_.text == "var" || current_.text == "let" || current_.text == "const")) {
        auto var_decl = parse_var_declaration();
        auto& ref = std::get<VarDeclStmt>(*var_decl);
        stmt->init_var_decl = std::make_unique<VarDeclStmt>(std::move(ref));
    } else if (current_.type != TokenType::SEMICOLON) {
        stmt->init_expr = parse_expression(0);
    }
    if (current_.type == TokenType::SEMICOLON) {
        advance();
    } else {
        error("expected ';' in for statement");
        synchronize();
    }

    if (current_.type != TokenType::SEMICOLON) {
        stmt->test = parse_expression(0);
    }
    if (current_.type == TokenType::SEMICOLON) {
        advance();
    } else {
        error("expected ';' in for statement");
        synchronize();
    }

    if (current_.type != TokenType::RPAREN) {
        stmt->update = parse_expression(0);
    }
    expect(TokenType::RPAREN);

    stmt->body = parse_statement();

    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// Return
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_return() {
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;
    advance(); // consume 'return'

    if (current_.type != TokenType::SEMICOLON &&
        current_.type != TokenType::RBRACE &&
        current_.type != TokenType::EOF_TOKEN) {
        stmt->argument = parse_expression(0);
    }

    if (current_.type == TokenType::SEMICOLON) {
        advance();
    } else if (current_.type != TokenType::RBRACE && current_.type != TokenType::EOF_TOKEN) {
        synchronize();
    }

    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// Break
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_break() {
    auto stmt = std::make_unique<BreakStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;
    advance(); // consume 'break'

    if (current_.type == TokenType::IDENTIFIER) {
        stmt->label = current_.text;
        advance();
    }

    if (current_.type == TokenType::SEMICOLON) {
        advance();
    } else if (current_.type != TokenType::RBRACE && current_.type != TokenType::EOF_TOKEN) {
        synchronize();
    }

    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// Throw
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_throw() {
    auto stmt = std::make_unique<ThrowStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;
    advance(); // consume 'throw'

    if (current_.type != TokenType::SEMICOLON &&
        current_.type != TokenType::RBRACE &&
        current_.type != TokenType::EOF_TOKEN) {
        stmt->argument = parse_expression(0);
    } else {
        error("expected expression after throw");
    }

    if (current_.type == TokenType::SEMICOLON) {
        advance();
    } else if (current_.type != TokenType::RBRACE && current_.type != TokenType::EOF_TOKEN) {
        synchronize();
    }

    return std::make_unique<Stmt>(std::move(*stmt));
}

// ---------------------------------------------------------------------------
// Try / Catch / Finally
// ---------------------------------------------------------------------------

std::unique_ptr<Stmt> Parser::parse_try() {
    auto stmt = std::make_unique<TryStmt>();
    stmt->line = current_.line;
    stmt->column = current_.column;
    advance(); // consume 'try'

    stmt->block = parse_block();

    if (current_.type == TokenType::IDENTIFIER && current_.text == "catch") {
        advance();
        auto handler = std::make_unique<CatchClause>();
        handler->line = current_.line;
        handler->column = current_.column;

        expect(TokenType::LPAREN);
        handler->param = parse_pattern();
        expect(TokenType::RPAREN);

        handler->body = parse_block();
        stmt->handler = std::move(handler);
    }

    if (current_.type == TokenType::IDENTIFIER && current_.text == "finally") {
        advance();
        stmt->finalizer = parse_block();
    }

    if (!stmt->handler && !stmt->finalizer) {
        error("try must have catch or finally");
    }

    return std::make_unique<Stmt>(std::move(*stmt));
}

} // namespace browser::js
