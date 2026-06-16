#include "test_framework.hpp"
#include "utility.hpp"
#include "../js/ast.hpp"
#include "../js/parser.hpp"

using namespace browser::js;

static LiteralExpr& as_literal(Expr& e) {
    return std::get<LiteralExpr>(e);
}
static IdentExpr& as_ident(Expr& e) {
    return std::get<IdentExpr>(e);
}
static BinaryExpr& as_binary(Expr& e) {
    return std::get<BinaryExpr>(e);
}
static UnaryExpr& as_unary(Expr& e) {
    return std::get<UnaryExpr>(e);
}
static CallExpr& as_call(Expr& e) {
    return std::get<CallExpr>(e);
}
static MemberExpr& as_member(Expr& e) {
    return std::get<MemberExpr>(e);
}
static AssignExpr& as_assign(Expr& e) {
    return std::get<AssignExpr>(e);
}
static ArrowFuncExpr& as_arrow(Expr& e) {
    return std::get<ArrowFuncExpr>(e);
}
static ArrLiteralExpr& as_array(Expr& e) {
    return std::get<ArrLiteralExpr>(e);
}
static VarDeclStmt& as_var_decl(Stmt& s) {
    return std::get<VarDeclStmt>(s);
}
static ExpressionStmt& as_expr_stmt(Stmt& s) {
    return std::get<ExpressionStmt>(s);
}
static FuncDeclStmt& as_func_decl(Stmt& s) {
    return std::get<FuncDeclStmt>(s);
}
static IfStmt& as_if(Stmt& s) {
    return std::get<IfStmt>(s);
}
static WhileStmt& as_while(Stmt& s) {
    return std::get<WhileStmt>(s);
}
static ForStmt& as_for(Stmt& s) {
    return std::get<ForStmt>(s);
}
static ReturnStmt& as_return(Stmt& s) {
    return std::get<ReturnStmt>(s);
}

// ---------------------------------------------------------------------------
// parse_literal
// ---------------------------------------------------------------------------

TEST(parse_literal_number, {
    Parser p("42;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& lit = as_literal(*stmt.expr);
    ASSERT_EQ(lit.type, LiteralExpr::Type::NUMBER);
    ASSERT_EQ(lit.number, 42.0);
})

TEST(parse_literal_string, {
    Parser p("\"hello\";");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& lit = as_literal(*stmt.expr);
    ASSERT_EQ(lit.type, LiteralExpr::Type::STRING);
    ASSERT_EQ(lit.string_val, "hello");
})

TEST(parse_literal_boolean, {
    Parser p("true;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& lit = as_literal(*stmt.expr);
    ASSERT_EQ(lit.type, LiteralExpr::Type::BOOLEAN);
    ASSERT_EQ(lit.bool_val, true);
})

TEST(parse_literal_null, {
    Parser p("null;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& lit = as_literal(*stmt.expr);
    ASSERT_EQ(lit.type, LiteralExpr::Type::NULL_VAL);
})

// ---------------------------------------------------------------------------
// parse_binary
// ---------------------------------------------------------------------------

TEST(parse_binary_precedence, {
    Parser p("1 + 2 * 3;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& bin = as_binary(*stmt.expr);
    ASSERT_EQ(as_literal(*bin.left).number, 1.0);
    auto& right_bin = as_binary(*bin.right);
    ASSERT_EQ(right_bin.op, TokenType::STAR);
    ASSERT_EQ(as_literal(*right_bin.left).number, 2.0);
    ASSERT_EQ(as_literal(*right_bin.right).number, 3.0);
})

TEST(parse_binary_add, {
    Parser p("1 + 2;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& bin = as_binary(*stmt.expr);
    ASSERT_EQ(bin.op, TokenType::PLUS);
})

// ---------------------------------------------------------------------------
// parse_grouping
// ---------------------------------------------------------------------------

TEST(parse_grouping, {
    Parser p("(1 + 2) * 3;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& bin = as_binary(*stmt.expr);
    auto& left_bin = as_binary(*bin.left);
    ASSERT_EQ(left_bin.op, TokenType::PLUS);
    ASSERT_EQ(as_literal(*left_bin.left).number, 1.0);
    ASSERT_EQ(as_literal(*left_bin.right).number, 2.0);
    ASSERT_EQ(as_literal(*bin.right).number, 3.0);
})

// ---------------------------------------------------------------------------
// parse_unary
// ---------------------------------------------------------------------------

TEST(parse_unary_minus, {
    Parser p("-42;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& unary = as_unary(*stmt.expr);
    ASSERT_EQ(unary.op, TokenType::MINUS);
    ASSERT(unary.prefix);
    ASSERT_EQ(as_literal(*unary.argument).number, 42.0);
})

TEST(parse_unary_not, {
    Parser p("!true;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& unary = as_unary(*stmt.expr);
    ASSERT_EQ(unary.op, TokenType::NOT);
})

TEST(parse_unary_typeof, {
    Parser p("typeof x;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& unary = as_unary(*stmt.expr);
    ASSERT_EQ(unary.op, TokenType::IDENTIFIER);
    ASSERT(as_ident(*unary.argument).name == "x");
})

// ---------------------------------------------------------------------------
// parse_var
// ---------------------------------------------------------------------------

TEST(parse_var_single, {
    Parser p("var x = 1;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    ASSERT_EQ(v.kind, VarDeclStmt::Kind::VAR);
    ASSERT_EQ(v.declarations.size(), 1u);
    ASSERT_EQ(std::get<IdentPattern>(*v.declarations[0].id).name, "x");
    ASSERT_EQ(as_literal(*v.declarations[0].init).number, 1.0);
})

TEST(parse_var_multiple, {
    Parser p("var a = 1, b = 2;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    ASSERT_EQ(v.declarations.size(), 2u);
    ASSERT_EQ(std::get<IdentPattern>(*v.declarations[0].id).name, "a");
    ASSERT_EQ(std::get<IdentPattern>(*v.declarations[1].id).name, "b");
})

TEST(parse_var_let, {
    Parser p("let y;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    ASSERT_EQ(v.kind, VarDeclStmt::Kind::LET);
    ASSERT_EQ(v.declarations.size(), 1u);
    ASSERT(!v.declarations[0].init);
})

TEST(parse_var_const, {
    Parser p("const z = 2;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    ASSERT_EQ(v.kind, VarDeclStmt::Kind::CONST);
    ASSERT_EQ(as_literal(*v.declarations[0].init).number, 2.0);
})

// ---------------------------------------------------------------------------
// parse_function
// ---------------------------------------------------------------------------

TEST(parse_function, {
    Parser p("function f(x) { return x + 1; }");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& f = as_func_decl(*prog->body[0]);
    ASSERT_EQ(f.name, "f");
    ASSERT_EQ(f.params.size(), 1u);
    ASSERT_EQ(std::get<IdentPattern>(*f.params[0]).name, "x");
    ASSERT_EQ(f.body->body.size(), 1u);
    auto& ret = as_return(*f.body->body[0]);
    auto& bin = as_binary(*ret.argument);
    ASSERT_EQ(bin.op, TokenType::PLUS);
})

// ---------------------------------------------------------------------------
// parse_if_else
// ---------------------------------------------------------------------------

TEST(parse_if_else, {
    Parser p("if (x) { a; } else { b; }");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_if(*prog->body[0]);
    ASSERT(as_ident(*stmt.test).name == "x");
    ASSERT(stmt.consequent != nullptr);
    ASSERT(stmt.alternate != nullptr);
})

TEST(parse_if_no_else, {
    Parser p("if (x) { a; }");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_if(*prog->body[0]);
    ASSERT(stmt.consequent != nullptr);
    ASSERT(!stmt.alternate);
})

// ---------------------------------------------------------------------------
// parse_while
// ---------------------------------------------------------------------------

TEST(parse_while, {
    Parser p("while (x) { y; }");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_while(*prog->body[0]);
    ASSERT(as_ident(*stmt.test).name == "x");
    ASSERT(stmt.body != nullptr);
})

// ---------------------------------------------------------------------------
// parse_for
// ---------------------------------------------------------------------------

TEST(parse_for, {
    Parser p("for (var i = 0; i < 10; i++) { body; }");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_for(*prog->body[0]);
    ASSERT(stmt.init_var_decl != nullptr);
    ASSERT(stmt.init_expr == nullptr);
    ASSERT_EQ(stmt.init_var_decl->declarations.size(), 1u);
    ASSERT(stmt.test != nullptr);
    ASSERT(stmt.update != nullptr);
    ASSERT(stmt.body != nullptr);
})

// ---------------------------------------------------------------------------
// parse_call
// ---------------------------------------------------------------------------

TEST(parse_call, {
    Parser p("foo(1, 2);");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& call = as_call(*stmt.expr);
    ASSERT(as_ident(*call.callee).name == "foo");
    ASSERT_EQ(call.args.size(), 2u);
})

TEST(parse_call_no_args, {
    Parser p("foo();");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& call = as_call(*stmt.expr);
    ASSERT_EQ(call.args.size(), 0u);
})

// ---------------------------------------------------------------------------
// parse_member
// ---------------------------------------------------------------------------

TEST(parse_member_dot, {
    Parser p("obj.prop;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& mem = as_member(*stmt.expr);
    ASSERT(as_ident(*mem.object).name == "obj");
    ASSERT(as_ident(*mem.property).name == "prop");
    ASSERT(!mem.computed);
})

TEST(parse_member_bracket, {
    Parser p("obj[42];");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& mem = as_member(*stmt.expr);
    ASSERT(as_ident(*mem.object).name == "obj");
    ASSERT(mem.computed);
    ASSERT_EQ(as_literal(*mem.property).number, 42.0);
})

// ---------------------------------------------------------------------------
// parse_assign
// ---------------------------------------------------------------------------

TEST(parse_assign, {
    Parser p("x = 42;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& assign = as_assign(*stmt.expr);
    ASSERT(as_ident(*assign.left).name == "x");
    ASSERT_EQ(as_literal(*assign.right).number, 42.0);
    ASSERT_EQ(assign.op, TokenType::EQUALS);
})

// ---------------------------------------------------------------------------
// parse_arrow
// ---------------------------------------------------------------------------

TEST(parse_arrow_single_param, {
    Parser p("const f = (x) => x * 2;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    ASSERT_EQ(v.declarations.size(), 1u);
    auto& arrow = as_arrow(*v.declarations[0].init);
    ASSERT_EQ(arrow.params.size(), 1u);
    ASSERT(arrow.is_expression_body);
    ASSERT(arrow.body_expr != nullptr);
})

TEST(parse_arrow_no_parens, {
    Parser p("const f = x => x + 1;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    auto& arrow = as_arrow(*v.declarations[0].init);
    ASSERT_EQ(arrow.params.size(), 1u);
})

// ---------------------------------------------------------------------------
// parse_object
// ---------------------------------------------------------------------------

TEST(parse_object, {
    Parser p("var x = {a: 1, b: 2};");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    ASSERT_EQ(v.declarations.size(), 1u);
    auto& obj = std::get<ObjLiteralExpr>(*v.declarations[0].init);
    ASSERT_EQ(obj.properties.size(), 2u);
    ASSERT_EQ(obj.properties[0].key, "a");
    ASSERT_EQ(as_literal(*obj.properties[0].value).number, 1.0);
    ASSERT_EQ(obj.properties[1].key, "b");
    ASSERT_EQ(as_literal(*obj.properties[1].value).number, 2.0);
})

// ---------------------------------------------------------------------------
// parse_array
// ---------------------------------------------------------------------------

TEST(parse_array, {
    Parser p("[1, 2, 3];");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& arr = as_array(*stmt.expr);
    ASSERT_EQ(arr.elements.size(), 3u);
    ASSERT_EQ(as_literal(*arr.elements[0]).number, 1.0);
    ASSERT_EQ(as_literal(*arr.elements[1]).number, 2.0);
    ASSERT_EQ(as_literal(*arr.elements[2]).number, 3.0);
})

// ---------------------------------------------------------------------------
// parse_template
// ---------------------------------------------------------------------------

TEST(parse_template, {
    Parser p("`hello ${name}`;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& tpl = std::get<TemplateExpr>(*stmt.expr);
    ASSERT_EQ(tpl.quasis.size(), 2u);
    ASSERT_EQ(tpl.quasis[0], "hello ");
    ASSERT_EQ(tpl.quasis[1], "");
    ASSERT_EQ(tpl.exprs.size(), 1u);
    ASSERT(as_ident(*tpl.exprs[0]).name == "name");
})

// ---------------------------------------------------------------------------
// parse_empty_program
// ---------------------------------------------------------------------------

TEST(parse_empty_program, {
    Parser p("");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 0u);
    ASSERT_EQ(p.errors().size(), 0u);
})

// ---------------------------------------------------------------------------
// parse_error_recovery
// ---------------------------------------------------------------------------

TEST(parse_error_recovery, {
    Parser p("var x = ; var y = 1;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 2u);
    ASSERT(p.errors().size() > 0u);
    auto& v1 = as_var_decl(*prog->body[0]);
    ASSERT_EQ(std::get<IdentPattern>(*v1.declarations[0].id).name, "x");
    auto& v2 = as_var_decl(*prog->body[1]);
    ASSERT_EQ(std::get<IdentPattern>(*v2.declarations[0].id).name, "y");
})

// ---------------------------------------------------------------------------
// parse_literal_undefined
// ---------------------------------------------------------------------------

TEST(parse_literal_undefined, {
    Parser p("undefined;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& lit = as_literal(*stmt.expr);
    ASSERT_EQ(lit.type, LiteralExpr::Type::UNDEFINED);
})

// ---------------------------------------------------------------------------
// parse_literal_bigint
// ---------------------------------------------------------------------------

TEST(parse_literal_bigint, {
    Parser p("123n;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& lit = as_literal(*stmt.expr);
    ASSERT_EQ(lit.type, LiteralExpr::Type::BIGINT);
    ASSERT_EQ(lit.string_val, "123n");
})

// ---------------------------------------------------------------------------
// parse_arrow_multi_param
// ---------------------------------------------------------------------------

TEST(parse_arrow_multi_param, {
    Parser p("const f = (a, b) => a + b;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    auto& arrow = as_arrow(*v.declarations[0].init);
    ASSERT_EQ(arrow.params.size(), 2u);
    ASSERT_EQ(std::get<IdentPattern>(*arrow.params[0]).name, "a");
    ASSERT_EQ(std::get<IdentPattern>(*arrow.params[1]).name, "b");
})

// ---------------------------------------------------------------------------
// parse_object_string_key
// ---------------------------------------------------------------------------

TEST(parse_object_string_key, {
    Parser p("var x = {\"key\": 1};");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& v = as_var_decl(*prog->body[0]);
    auto& obj = std::get<ObjLiteralExpr>(*v.declarations[0].init);
    ASSERT_EQ(obj.properties.size(), 1u);
    ASSERT_EQ(obj.properties[0].key, "key");
})

// ---------------------------------------------------------------------------
// parse_member_optional
// ---------------------------------------------------------------------------

TEST(parse_member_optional, {
    Parser p("obj?.prop;");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& mem = as_member(*stmt.expr);
    ASSERT(as_ident(*mem.object).name == "obj");
    ASSERT(as_ident(*mem.property).name == "prop");
    ASSERT(mem.optional);
})

// ---------------------------------------------------------------------------
// parse_new_member
// ---------------------------------------------------------------------------

TEST(parse_new_member, {
    Parser p("new Foo.Bar();");
    auto prog = p.parse_program();
    ASSERT_EQ(prog->body.size(), 1u);
    ASSERT_EQ(p.errors().size(), 0u);
    auto& stmt = as_expr_stmt(*prog->body[0]);
    auto& call = as_call(*stmt.expr);
    auto& mem = as_member(*call.callee);
    ASSERT(as_ident(*mem.object).name == "Foo");
    ASSERT(as_ident(*mem.property).name == "Bar");
})
