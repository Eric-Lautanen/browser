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
// Precedence
// ---------------------------------------------------------------------------

static int get_precedence(TokenType type) {
    switch (type) {
        case TokenType::EQUALS:
        case TokenType::PLUS_EQ:
        case TokenType::MINUS_EQ:
        case TokenType::STAR_EQ:
        case TokenType::SLASH_EQ:
        case TokenType::PERCENT_EQ:
            return 1;
        case TokenType::NULLISH_COALESCING:
            return 2;
        case TokenType::OR_OR:
            return 3;
        case TokenType::AND_AND:
            return 4;
        case TokenType::PIPE:
            return 5;
        case TokenType::CARET:
            return 6;
        case TokenType::AMPERSAND:
            return 7;
        case TokenType::EQ_EQ:
        case TokenType::NOT_EQ:
        case TokenType::EQ_EQ_EQ:
        case TokenType::NOT_EQ_EQ:
            return 8;
        case TokenType::LT:
        case TokenType::GT:
        case TokenType::LT_EQ:
        case TokenType::GT_EQ:
        case TokenType::INSTANCEOF:
            return 10;
        case TokenType::PLUS:
        case TokenType::MINUS:
            return 11;
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::PERCENT:
            return 12;
        default:
            return 0;
    }
}

static bool is_assignment_op(TokenType type) {
    return type == TokenType::EQUALS ||
           type == TokenType::PLUS_EQ ||
           type == TokenType::MINUS_EQ ||
           type == TokenType::STAR_EQ ||
           type == TokenType::SLASH_EQ ||
           type == TokenType::PERCENT_EQ;
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

// ---------------------------------------------------------------------------
// Expression parsing
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parse_expression(int min_precedence) {
    auto left = parse_primary();
    if (!left) return nullptr;

    left = parse_postfix_expr(std::move(left));

    // Tagged template (TEMPLATE_HEAD only)
    if (current_.type == TokenType::TEMPLATE_HEAD) {
        auto tpl = parse_template();
        if (tpl) {
            auto call = std::make_unique<CallExpr>();
            call->callee = std::move(left);
            call->args.push_back(std::move(tpl));
            left = std::make_unique<Expr>(std::move(*call));
        }
    }

    left = parse_binary_rhs(std::move(left), min_precedence);

    return left;
}

// ---------------------------------------------------------------------------
// Postfix
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parse_postfix_expr(std::unique_ptr<Expr> left) {
    if (!left) return nullptr;

    for (;;) {
        if (current_.type == TokenType::DOT) {
            advance();
            if (current_.type == TokenType::IDENTIFIER) {
                auto prop = std::make_unique<IdentExpr>();
                prop->name = current_.text;
                advance();
                auto mem = std::make_unique<MemberExpr>();
                mem->object = std::move(left);
                mem->property = std::make_unique<Expr>(std::move(*prop));
                mem->computed = false;
                left = std::make_unique<Expr>(std::move(*mem));
            } else {
                error("expected property name after '.'");
                break;
            }
        } else if (current_.type == TokenType::QUESTION_DOT) {
            advance();
            if (current_.type == TokenType::IDENTIFIER) {
                auto prop = std::make_unique<IdentExpr>();
                prop->name = current_.text;
                advance();
                auto mem = std::make_unique<MemberExpr>();
                mem->object = std::move(left);
                mem->property = std::make_unique<Expr>(std::move(*prop));
                mem->computed = false;
                mem->optional = true;
                left = std::make_unique<Expr>(std::move(*mem));
            } else {
                error("expected property name after '?.'");
                break;
            }
        } else if (current_.type == TokenType::LBRACKET) {
            advance();
            auto prop = parse_expression(0);
            if (!prop) break;
            expect(TokenType::RBRACKET);
            auto mem = std::make_unique<MemberExpr>();
            mem->object = std::move(left);
            mem->property = std::move(prop);
            mem->computed = true;
            left = std::make_unique<Expr>(std::move(*mem));
        } else if (current_.type == TokenType::LPAREN) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            if (current_.type != TokenType::RPAREN) {
                for (;;) {
                    auto arg = parse_expression(0);
                    if (arg) args.push_back(std::move(arg));
                    if (!match(TokenType::COMMA)) break;
                }
            }
            expect(TokenType::RPAREN);
            auto call = std::make_unique<CallExpr>();
            call->callee = std::move(left);
            call->args = std::move(args);
            left = std::make_unique<Expr>(std::move(*call));
        } else if (current_.type == TokenType::PLUS_PLUS || current_.type == TokenType::MINUS_MINUS) {
            auto op = current_.type;
            advance();
            auto unary = std::make_unique<UnaryExpr>();
            unary->argument = std::move(left);
            unary->op = op;
            unary->prefix = false;
            left = std::make_unique<Expr>(std::move(*unary));
        } else {
            break;
        }
    }

    return left;
}

// ---------------------------------------------------------------------------
// Binary/assignment RHS
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parse_binary_rhs(std::unique_ptr<Expr> left, int min_precedence) {
    if (!left) return nullptr;

    while (current_.type != TokenType::EOF_TOKEN) {
        auto op = current_.type;
        int prec = get_precedence(op);
        if (prec == 0 || prec < min_precedence) break;

        if (is_assignment_op(op)) {
            // Right-associative: parse RHS with same precedence (not prec+1)
            advance();
            auto right = parse_expression(prec);
            if (!right) break;
            auto assign = std::make_unique<AssignExpr>();
            assign->left = std::move(left);
            assign->right = std::move(right);
            assign->op = op;
            left = std::make_unique<Expr>(std::move(*assign));
        } else {
            // Left-associative: parse RHS with prec+1
            advance();
            auto right = parse_expression(prec + 1);
            if (!right) break;
            auto bin = std::make_unique<BinaryExpr>();
            bin->left = std::move(left);
            bin->right = std::move(right);
            bin->op = op;
            left = std::make_unique<Expr>(std::move(*bin));
        }
    }

    return left;
}

// ---------------------------------------------------------------------------
// Arrow body helper
// ---------------------------------------------------------------------------

void Parser::parse_arrow_body(ArrowFuncExpr* arrow) {
    if (current_.type == TokenType::LBRACE) {
        auto block_stmt = parse_block();
        auto& block = std::get<BlockStmt>(*block_stmt);
        arrow->body_block = std::make_unique<BlockStmt>();
        arrow->body_block->body = std::move(block.body);
        arrow->is_expression_body = false;
    } else {
        arrow->body_expr = parse_expression(0);
        arrow->is_expression_body = true;
    }
}

// ---------------------------------------------------------------------------
// Primary expressions
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parse_primary() {
    u32 line = current_.line;
    u32 col = current_.column;

    // Handle "new" keyword
    if (current_.type == TokenType::IDENTIFIER && current_.text == "new") {
        return parse_new_expr();
    }

    switch (current_.type) {
        case TokenType::NUMBER: {
            auto lit = std::make_unique<LiteralExpr>();
            lit->line = line; lit->column = col;
            lit->type = LiteralExpr::Type::NUMBER;
            lit->number = current_.numeric_value;
            advance();
            return std::make_unique<Expr>(std::move(*lit));
        }
        case TokenType::STRING: {
            auto lit = std::make_unique<LiteralExpr>();
            lit->line = line; lit->column = col;
            lit->type = LiteralExpr::Type::STRING;
            lit->string_val = current_.text;
            advance();
            return std::make_unique<Expr>(std::move(*lit));
        }
        case TokenType::BOOLEAN: {
            auto lit = std::make_unique<LiteralExpr>();
            lit->line = line; lit->column = col;
            lit->type = LiteralExpr::Type::BOOLEAN;
            lit->bool_val = (current_.text == "true");
            advance();
            return std::make_unique<Expr>(std::move(*lit));
        }
        case TokenType::NULL_LITERAL: {
            auto lit = std::make_unique<LiteralExpr>();
            lit->line = line; lit->column = col;
            lit->type = LiteralExpr::Type::NULL_VAL;
            advance();
            return std::make_unique<Expr>(std::move(*lit));
        }
        case TokenType::UNDEFINED: {
            auto lit = std::make_unique<LiteralExpr>();
            lit->line = line; lit->column = col;
            lit->type = LiteralExpr::Type::UNDEFINED;
            advance();
            return std::make_unique<Expr>(std::move(*lit));
        }
        case TokenType::BIGINT: {
            auto lit = std::make_unique<LiteralExpr>();
            lit->line = line; lit->column = col;
            lit->type = LiteralExpr::Type::BIGINT;
            lit->string_val = current_.text;
            advance();
            return std::make_unique<Expr>(std::move(*lit));
        }
        case TokenType::REGEXP: {
            auto lit = std::make_unique<LiteralExpr>();
            lit->line = line; lit->column = col;
            lit->type = LiteralExpr::Type::REGEXP;
            lit->string_val = current_.text;
            advance();
            return std::make_unique<Expr>(std::move(*lit));
        }
        case TokenType::LBRACKET: {
            advance();
            auto arr = std::make_unique<ArrLiteralExpr>();
            arr->line = line; arr->column = col;
            while (current_.type != TokenType::RBRACKET && current_.type != TokenType::EOF_TOKEN) {
                if (current_.type == TokenType::COMMA) {
                    arr->elements.push_back(nullptr);
                    advance();
                    continue;
                }
                auto elem = parse_expression(0);
                if (elem) arr->elements.push_back(std::move(elem));
                if (!match(TokenType::COMMA)) break;
            }
            expect(TokenType::RBRACKET);
            return std::make_unique<Expr>(std::move(*arr));
        }
        case TokenType::LBRACE: {
            advance();
            auto obj = std::make_unique<ObjLiteralExpr>();
            obj->line = line; obj->column = col;
            while (current_.type != TokenType::RBRACE && current_.type != TokenType::EOF_TOKEN) {
                if (current_.type == TokenType::IDENTIFIER) {
                    ObjLiteralExpr::Property prop;
                    prop.key = current_.text;
                    advance();
                    if (match(TokenType::COLON)) {
                        prop.value = parse_expression(0);
                    }
                    obj->properties.push_back(std::move(prop));
                } else if (current_.type == TokenType::STRING) {
                    ObjLiteralExpr::Property prop;
                    prop.key = current_.text;
                    advance();
                    if (match(TokenType::COLON)) {
                        prop.value = parse_expression(0);
                    }
                    obj->properties.push_back(std::move(prop));
                } else if (current_.type == TokenType::LBRACKET) {
                    advance();
                    ObjLiteralExpr::Property prop;
                    // For computed keys, store the expression text as key placeholder
                    auto key_expr = parse_expression(0);
                    if (key_expr) {
                        // Use a representation of the computed expression as key
                        std::ostringstream oss;
                        oss << "[computed:" << current_.line << "]";
                        prop.key = oss.str();
                    }
                    expect(TokenType::RBRACKET);
                    if (match(TokenType::COLON)) {
                        prop.value = parse_expression(0);
                    }
                    obj->properties.push_back(std::move(prop));
                } else if (current_.type == TokenType::NUMBER) {
                    ObjLiteralExpr::Property prop;
                    std::ostringstream oss;
                    oss << current_.numeric_value;
                    prop.key = oss.str();
                    advance();
                    if (match(TokenType::COLON)) {
                        prop.value = parse_expression(0);
                    }
                    obj->properties.push_back(std::move(prop));
                } else {
                    error("expected property name in object literal");
                    break;
                }
                if (!match(TokenType::COMMA)) break;
            }
            expect(TokenType::RBRACE);
            return std::make_unique<Expr>(std::move(*obj));
        }
        case TokenType::TEMPLATE_HEAD:
        case TokenType::TEMPLATE_TAIL: {
            return parse_template();
        }
        case TokenType::NOT:
        case TokenType::TILDE:
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS: {
            auto op = current_.type;
            advance();
            auto unary = std::make_unique<UnaryExpr>();
            unary->line = line; unary->column = col;
            unary->op = op;
            unary->prefix = true;
            // Parse operand with high precedence so it doesn't consume
            // binary operators that should bind in the outer expression.
            // e.g. ++x * 2  →  (++x) * 2  (not ++(x * 2))
            unary->argument = parse_expression(13);
            if (!unary->argument) return nullptr;
            return std::make_unique<Expr>(std::move(*unary));
        }
        case TokenType::LPAREN: {
            advance();

            // Empty parens: ()
            if (current_.type == TokenType::RPAREN) {
                advance();
                if (current_.type == TokenType::ARROW) {
                    advance();
                    auto arrow = std::make_unique<ArrowFuncExpr>();
                    arrow->line = line; arrow->column = col;
                    parse_arrow_body(arrow.get());
                    return std::make_unique<Expr>(std::move(*arrow));
                }
                error("empty parentheses not valid outside arrow function");
                return nullptr;
            }

            // Decide: is this likely an arrow param list or a parenthesized expression?
            // Arrow params start with IDENTIFIER, LBRACE, or LBRACKET (pattern starts).
            // Everything else (NUMBER, STRING, unary ops, etc.) is a grouping expr.
            bool could_be_arrow = (current_.type == TokenType::IDENTIFIER ||
                                   current_.type == TokenType::LBRACE ||
                                   current_.type == TokenType::LBRACKET);

            if (could_be_arrow) {
                // Try to parse as arrow function params first.
                std::vector<std::unique_ptr<Pattern>> arrow_params;
                bool is_arrow = false;

                auto first_param = parse_pattern_or_ident();
                if (!first_param) {
                    error("expected expression or arrow parameter");
                    return nullptr;
                }

                if (current_.type == TokenType::COMMA) {
                    // Multi-param arrow: (a, b, ...) =>
                    arrow_params.push_back(std::move(first_param));
                    while (match(TokenType::COMMA)) {
                        auto p = parse_pattern();
                        if (p) arrow_params.push_back(std::move(p));
                    }
                    if (current_.type == TokenType::RPAREN) {
                        advance();
                        if (current_.type == TokenType::ARROW) {
                            advance();
                            is_arrow = true;
                        }
                    }
                    if (is_arrow) {
                        auto arrow = std::make_unique<ArrowFuncExpr>();
                        arrow->line = line; arrow->column = col;
                        arrow->params = std::move(arrow_params);
                        parse_arrow_body(arrow.get());
                        return std::make_unique<Expr>(std::move(*arrow));
                    }
                    // (a, b) without => — not valid as grouping; error
                    error("expected '=>' after parenthesized parameter list");
                    return nullptr;
                }

                // Single element inside parens
                if (current_.type == TokenType::RPAREN) {
                    advance();
                    if (current_.type == TokenType::ARROW) {
                        advance();
                        is_arrow = true;
                    }
                }

                if (is_arrow) {
                    auto arrow = std::make_unique<ArrowFuncExpr>();
                    arrow->line = line; arrow->column = col;
                    if (first_param && std::holds_alternative<IdentPattern>(*first_param)) {
                        arrow->params.push_back(std::move(first_param));
                    }
                    parse_arrow_body(arrow.get());
                    return std::make_unique<Expr>(std::move(*arrow));
                }

                // Not an arrow — convert param back to identifier expression for grouping
                if (first_param && std::holds_alternative<IdentPattern>(*first_param)) {
                    auto& ip = std::get<IdentPattern>(*first_param);
                    auto ident = std::make_unique<IdentExpr>();
                    ident->line = ip.line;
                    ident->column = ip.column;
                    ident->name = ip.name;
                    return std::make_unique<Expr>(std::move(*ident));
                }
                if (first_param && std::holds_alternative<ObjPattern>(*first_param)) {
                    // ({a, b}) creates an object literal expression from the pattern
                    // For now, just error since this is rare
                    error("destructuring pattern not valid as expression");
                    return nullptr;
                }
                if (first_param && std::holds_alternative<ArrPattern>(*first_param)) {
                    error("destructuring pattern not valid as expression");
                    return nullptr;
                }

                error("expected expression inside parentheses");
                return nullptr;
            }

            // Not a param list — parse as parenthesized expression
            auto expr = parse_expression(0);
            if (!expr) {
                error("expected expression");
                return nullptr;
            }
            if (current_.type == TokenType::RPAREN) {
                advance();
                return expr;
            }
            error("expected ')' after expression");
            synchronize();
            return nullptr;
        }
        case TokenType::IDENTIFIER: {
            // Keyword-unary operators: typeof, void, delete
            if (current_.text == "typeof" || current_.text == "void" || current_.text == "delete") {
                advance();
                auto unary = std::make_unique<UnaryExpr>();
                unary->line = line; unary->column = col;
                unary->op = TokenType::IDENTIFIER;
                unary->prefix = true;
                unary->argument = parse_expression(13);
                if (!unary->argument) return nullptr;
                return std::make_unique<Expr>(std::move(*unary));
            }

            // Single-identifier arrow: x => ...
            auto name = current_.text;
            advance();
            if (current_.type == TokenType::ARROW) {
                advance();
                auto arrow = std::make_unique<ArrowFuncExpr>();
                arrow->line = line; arrow->column = col;
                arrow->params.push_back(
                    std::make_unique<Pattern>(IdentPattern{name}));
                parse_arrow_body(arrow.get());
                return std::make_unique<Expr>(std::move(*arrow));
            }

            auto ident = std::make_unique<IdentExpr>();
            ident->line = line; ident->column = col;
            ident->name = name;
            return std::make_unique<Expr>(std::move(*ident));
        }
        default: {
            std::ostringstream oss;
            oss << "unexpected token '" << current_.text << "' at line " << current_.line;
            error(oss.str());
            return nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// new expression
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parse_new_expr() {
    u32 line = current_.line;
    u32 col = current_.column;
    advance(); // consume 'new'

    auto callee = parse_primary();
    if (!callee) {
        error("expected expression after 'new'");
        return nullptr;
    }
    // Allow member access on the callee: new Foo.Bar()
    callee = parse_postfix_expr(std::move(callee));

    std::vector<std::unique_ptr<Expr>> args;
    if (current_.type == TokenType::LPAREN) {
        advance();
        while (current_.type != TokenType::RPAREN && current_.type != TokenType::EOF_TOKEN) {
            auto arg = parse_expression(0);
            if (arg) args.push_back(std::move(arg));
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RPAREN);
    }

    auto call = std::make_unique<CallExpr>();
    call->line = line;
    call->column = col;
    call->callee = std::move(callee);
    call->args = std::move(args);
    call->is_new = true;
    return std::make_unique<Expr>(std::move(*call));
}

// ---------------------------------------------------------------------------
// Template literals
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parse_template() {
    auto tpl = std::make_unique<TemplateExpr>();
    tpl->line = current_.line;
    tpl->column = current_.column;

    if (current_.type == TokenType::TEMPLATE_TAIL) {
        tpl->quasis.push_back(current_.text);
        advance();
        return std::make_unique<Expr>(std::move(*tpl));
    }

    if (current_.type == TokenType::TEMPLATE_HEAD) {
        tpl->quasis.push_back(current_.text);
        advance();

        while (current_.type != TokenType::TEMPLATE_TAIL &&
               current_.type != TokenType::EOF_TOKEN) {
            auto expr = parse_expression(0);
            if (expr) {
                tpl->exprs.push_back(std::move(expr));
            }

            if (current_.type == TokenType::TEMPLATE_MIDDLE) {
                tpl->quasis.push_back(current_.text);
                advance();
            } else if (current_.type == TokenType::TEMPLATE_TAIL) {
                tpl->quasis.push_back(current_.text);
                advance();
                break;
            } else {
                error("expected template continuation or end");
                break;
            }
        }
    }

    return std::make_unique<Expr>(std::move(*tpl));
}

// ---------------------------------------------------------------------------
// Pattern-or-identifier: used for arrow function params that might be
// simple identifiers or destructuring patterns
// ---------------------------------------------------------------------------

std::unique_ptr<Pattern> Parser::parse_pattern_or_ident() {
    if (current_.type == TokenType::IDENTIFIER) {
        auto ident = std::make_unique<IdentPattern>();
        ident->line = current_.line;
        ident->column = current_.column;
        ident->name = current_.text;
        advance();
        return std::make_unique<Pattern>(std::move(*ident));
    }
    return parse_pattern();
}

// ---------------------------------------------------------------------------
// Pattern parsing
// ---------------------------------------------------------------------------

std::unique_ptr<Pattern> Parser::parse_pattern() {
    if (current_.type == TokenType::LBRACE) {
        advance();
        auto obj = std::make_unique<ObjPattern>();
        obj->line = current_.line;
        obj->column = current_.column;
        while (current_.type != TokenType::RBRACE && current_.type != TokenType::EOF_TOKEN) {
            if (current_.type == TokenType::IDENTIFIER) {
                ObjPattern::Property prop;
                prop.key = current_.text;
                advance();
                if (match(TokenType::COLON)) {
                    prop.value = parse_pattern();
                } else {
                    auto ip = std::make_unique<IdentPattern>();
                    ip->name = prop.key;
                    prop.value = std::make_unique<Pattern>(std::move(*ip));
                }
                obj->properties.push_back(std::move(prop));
            } else {
                error("expected property name in object pattern");
                break;
            }
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RBRACE);
        return std::make_unique<Pattern>(std::move(*obj));
    }

    if (current_.type == TokenType::LBRACKET) {
        advance();
        auto arr = std::make_unique<ArrPattern>();
        arr->line = current_.line;
        arr->column = current_.column;
        while (current_.type != TokenType::RBRACKET && current_.type != TokenType::EOF_TOKEN) {
            if (current_.type == TokenType::COMMA) {
                arr->elements.push_back(nullptr);
                advance();
                continue;
            }
            auto elem = parse_pattern();
            if (elem) arr->elements.push_back(std::move(elem));
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RBRACKET);
        return std::make_unique<Pattern>(std::move(*arr));
    }

    if (current_.type == TokenType::IDENTIFIER) {
        auto ident = std::make_unique<IdentPattern>();
        ident->line = current_.line;
        ident->column = current_.column;
        ident->name = current_.text;
        advance();
        return std::make_unique<Pattern>(std::move(*ident));
    }

    error("expected pattern");
    return nullptr;
}

} // namespace browser::js
