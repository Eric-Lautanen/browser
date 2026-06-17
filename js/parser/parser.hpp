#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../ast.hpp"
#include "../lexer.hpp"

namespace browser::js {

class Parser {
public:
    explicit Parser(const std::string& source);
    std::unique_ptr<Program> parse_program();
    std::vector<std::string> errors() const { return errors_; }

private:
    Lexer lexer_;
    Token current_;
    u32 brace_depth_ = 0;

    void advance();
    void expect(TokenType type);
    bool match(TokenType type);

    std::unique_ptr<Stmt> parse_statement();
    std::unique_ptr<Stmt> parse_var_declaration();
    std::unique_ptr<Stmt> parse_function_declaration();
    std::unique_ptr<Stmt> parse_if();
    std::unique_ptr<Stmt> parse_while();
    std::unique_ptr<Stmt> parse_block();
    std::unique_ptr<Stmt> parse_return();
    std::unique_ptr<Stmt> parse_break();
    std::unique_ptr<Stmt> parse_throw();
    std::unique_ptr<Stmt> parse_try();
    std::unique_ptr<Stmt> parse_for();

    std::unique_ptr<Expr> parse_expression(int min_precedence = 1);
    std::unique_ptr<Expr> parse_primary();
    std::unique_ptr<Expr> parse_postfix_expr(std::unique_ptr<Expr> left);
    std::unique_ptr<Expr> parse_binary_rhs(std::unique_ptr<Expr> left, int min_precedence);
    std::unique_ptr<Expr> parse_template();
    std::unique_ptr<Expr> parse_new_expr();

    void parse_arrow_body(ArrowFuncExpr* arrow);

    std::unique_ptr<Pattern> parse_pattern();
    std::unique_ptr<Pattern> parse_pattern_or_ident();

    void synchronize();
    void error(const std::string& msg);

    std::vector<std::string> errors_;
};

} // namespace browser::js
