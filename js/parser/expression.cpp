#include "parser.hpp"
#include <sstream>

namespace browser::js {

// ---------------------------------------------------------------------------
// Precedence helpers
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

} // namespace browser::js
