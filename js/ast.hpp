#pragma once
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include "../tests/utility.hpp"
#include "token.hpp"

namespace browser::js {

struct Node { virtual ~Node() = default; u32 line = 0; u32 column = 0; };

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

struct ExpressionStmt; struct BlockStmt; struct IfStmt;
struct WhileStmt; struct ForStmt; struct VarDeclStmt;
struct FuncDeclStmt; struct ReturnStmt; struct BreakStmt;
struct ThrowStmt; struct TryStmt; struct EmptyStmt;

struct LiteralExpr; struct IdentExpr; struct BinaryExpr;
struct UnaryExpr; struct CallExpr; struct MemberExpr;
struct AssignExpr; struct ArrowFuncExpr; struct ObjLiteralExpr;
struct ArrLiteralExpr; struct TemplateExpr;

struct IdentPattern; struct ObjPattern; struct ArrPattern;

struct Stmt;
struct Expr;
struct Pattern;

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

struct LiteralExpr : Node {
    enum class Type { NUMBER, STRING, BOOLEAN, NULL_VAL, UNDEFINED, BIGINT, REGEXP };
    Type type = Type::NUMBER;
    f64 number = 0;
    std::string string_val;
    bool bool_val = false;
};

struct IdentExpr : Node {
    std::string name;
};

struct BinaryExpr : Node {
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    TokenType op = TokenType::EOF_TOKEN;
};

struct UnaryExpr : Node {
    std::unique_ptr<Expr> argument;
    TokenType op = TokenType::EOF_TOKEN;
    bool prefix = true;
};

struct CallExpr : Node {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
};

struct MemberExpr : Node {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> property;
    bool computed = false;
    bool optional = false;
};

struct AssignExpr : Node {
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    TokenType op = TokenType::EOF_TOKEN;
};

struct ArrowFuncExpr : Node {
    std::vector<std::unique_ptr<Pattern>> params;
    std::unique_ptr<Expr> body_expr;
    std::unique_ptr<BlockStmt> body_block;
    bool is_expression_body = true;
};

struct ObjLiteralExpr : Node {
    struct Property {
        std::string key;
        std::unique_ptr<Expr> value;
    };
    std::vector<Property> properties;
};

struct ArrLiteralExpr : Node {
    std::vector<std::unique_ptr<Expr>> elements;
};

struct TemplateExpr : Node {
    std::vector<std::string> quasis;
    std::vector<std::unique_ptr<Expr>> exprs;
};

// ---------------------------------------------------------------------------
// Expr variant
// ---------------------------------------------------------------------------

struct Expr : std::variant<
    LiteralExpr, IdentExpr, BinaryExpr, UnaryExpr, CallExpr,
    MemberExpr, AssignExpr, ArrowFuncExpr, ObjLiteralExpr, ArrLiteralExpr, TemplateExpr
> {
    using variant::variant;
    Expr() = default;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

struct ExpressionStmt : Node {
    std::unique_ptr<Expr> expr;
};

struct BlockStmt : Node {
    std::vector<std::unique_ptr<Stmt>> body;
};

struct IfStmt : Node {
    std::unique_ptr<Expr> test;
    std::unique_ptr<Stmt> consequent;
    std::unique_ptr<Stmt> alternate;
};

struct WhileStmt : Node {
    std::unique_ptr<Expr> test;
    std::unique_ptr<Stmt> body;
};

struct ForStmt : Node {
    std::unique_ptr<Expr> init_expr;
    std::unique_ptr<VarDeclStmt> init_var_decl;
    std::unique_ptr<Expr> test;
    std::unique_ptr<Expr> update;
    std::unique_ptr<Stmt> body;
};

struct VarDeclStmt : Node {
    enum class Kind { VAR, LET, CONST };
    Kind kind = Kind::VAR;
    struct Declarator {
        std::unique_ptr<Pattern> id;
        std::unique_ptr<Expr> init;
    };
    std::vector<Declarator> declarations;
};

struct FuncDeclStmt : Node {
    std::string name;
    std::vector<std::unique_ptr<Pattern>> params;
    std::unique_ptr<BlockStmt> body;
    bool is_async = false;
    bool is_generator = false;
};

struct ReturnStmt : Node {
    std::unique_ptr<Expr> argument;
};

struct BreakStmt : Node {
    std::string label;
};

struct ThrowStmt : Node {
    std::unique_ptr<Expr> argument;
};

struct CatchClause : Node {
    std::unique_ptr<Pattern> param;
    std::unique_ptr<Stmt> body;
};

struct TryStmt : Node {
    std::unique_ptr<Stmt> block;
    std::unique_ptr<CatchClause> handler;
    std::unique_ptr<Stmt> finalizer;
};

struct EmptyStmt : Node {};

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

struct IdentPattern : Node {
    std::string name;
    IdentPattern() = default;
    IdentPattern(std::string n) : name(std::move(n)) {}
};

struct ObjPattern : Node {
    struct Property {
        std::string key;
        std::unique_ptr<Pattern> value;
    };
    std::vector<Property> properties;
};

struct ArrPattern : Node {
    std::vector<std::unique_ptr<Pattern>> elements;
};

// ---------------------------------------------------------------------------
// Stmt and Pattern variants
// ---------------------------------------------------------------------------

struct Stmt : std::variant<
    ExpressionStmt, BlockStmt, IfStmt, WhileStmt, ForStmt,
    VarDeclStmt, FuncDeclStmt, ReturnStmt, BreakStmt, ThrowStmt, TryStmt, EmptyStmt
> {
    using variant::variant;
    Stmt() = default;
};

struct Pattern : std::variant<
    IdentPattern, ObjPattern, ArrPattern
> {
    using variant::variant;
    Pattern() = default;
};

// ---------------------------------------------------------------------------
// Program
// ---------------------------------------------------------------------------

struct Program : Node {
    std::vector<std::unique_ptr<Stmt>> body;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

template<typename T, typename V>
T& as(V& v) {
    return std::get<T>(v);
}

template<typename T, typename V>
const T& as(const V& v) {
    return std::get<T>(v);
}

} // namespace browser::js
