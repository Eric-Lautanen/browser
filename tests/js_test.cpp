#include "test_framework.hpp"
#include "utility.hpp"
#include "../js/token.hpp"
#include "../js/lexer.hpp"
#include "../js/value.hpp"
#include "../js/vm.hpp"
#include "../js/gc.hpp"

using namespace browser::js;

static std::vector<Token> tokenize(const std::string& src) {
    std::vector<Token> tokens;
    Lexer l(src);
    while (l.has_next()) {
        tokens.push_back(l.next());
    }
    Token eof = l.next();
    if (eof.type == TokenType::EOF_TOKEN) tokens.push_back(eof);
    return tokens;
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

TEST(lex_number_decimal, {
    auto t = tokenize("42");
    ASSERT_EQ(t.size(), 2u);
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].text, "42");
    ASSERT_EQ(t[0].numeric_value, 42.0);
    ASSERT_EQ(t[1].type, TokenType::EOF_TOKEN);
})

TEST(lex_number_float, {
    auto t = tokenize("3.14");
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].text, "3.14");
    ASSERT(t[0].numeric_value > 3.13);
    ASSERT(t[0].numeric_value < 3.15);
})

TEST(lex_number_leading_dot, {
    auto t = tokenize(".5");
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].text, ".5");
    ASSERT_EQ(t[0].numeric_value, 0.5);
})

TEST(lex_number_hex, {
    auto t = tokenize("0x1A");
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].text, "0x1A");
    ASSERT_EQ(t[0].numeric_value, 26.0);
})

TEST(lex_number_hex_lower, {
    auto t = tokenize("0xff");
    ASSERT_EQ(t[0].numeric_value, 255.0);
})

TEST(lex_number_octal, {
    auto t = tokenize("0o77");
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].text, "0o77");
    ASSERT_EQ(t[0].numeric_value, 63.0);
})

TEST(lex_number_binary, {
    auto t = tokenize("0b1010");
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].text, "0b1010");
    ASSERT_EQ(t[0].numeric_value, 10.0);
})

TEST(lex_number_exponent, {
    auto t = tokenize("1e2");
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].numeric_value, 100.0);
})

TEST(lex_number_exponent_neg, {
    auto t = tokenize("1e-2");
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].numeric_value, 0.01);
})

TEST(lex_number_bigint, {
    auto t = tokenize("123n");
    ASSERT_EQ(t[0].type, TokenType::BIGINT);
    ASSERT_EQ(t[0].text, "123n");
})

TEST(lex_number_hex_bigint, {
    auto t = tokenize("0xFFn");
    ASSERT_EQ(t[0].type, TokenType::BIGINT);
    ASSERT_EQ(t[0].text, "0xFFn");
})

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

TEST(lex_string_double, {
    auto t = tokenize("\"hello\"");
    ASSERT_EQ(t[0].type, TokenType::STRING);
    ASSERT_EQ(t[0].text, "hello");
})

TEST(lex_string_single, {
    auto t = tokenize("'world'");
    ASSERT_EQ(t[0].type, TokenType::STRING);
    ASSERT_EQ(t[0].text, "world");
})

TEST(lex_string_escape_newline, {
    auto t = tokenize("\"a\\nb\"");
    ASSERT_EQ(t[0].text, "a\nb");
})

TEST(lex_string_escape_tab, {
    auto t = tokenize("\"a\\tb\"");
    ASSERT_EQ(t[0].text, "a\tb");
})

TEST(lex_string_escape_quote, {
    auto t = tokenize("\"\\\\\\\"\"");
    ASSERT_EQ(t[0].text, "\\\"");
})

TEST(lex_string_escape_hex, {
    auto t = tokenize("\"\\x41\"");
    ASSERT_EQ(t[0].text, "A");
})

TEST(lex_string_escape_unicode, {
    auto t = tokenize("\"\\u0041\"");
    ASSERT_EQ(t[0].text, "A");
})

TEST(lex_string_escape_unicode_brace, {
    auto t = tokenize("\"\\u{41}\"");
    ASSERT_EQ(t[0].text, "A");
})

// ---------------------------------------------------------------------------
// Identifiers and keywords
// ---------------------------------------------------------------------------

TEST(lex_identifier_basic, {
    auto t = tokenize("foo");
    ASSERT_EQ(t[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[0].text, "foo");
})

TEST(lex_identifier_with_digits, {
    auto t = tokenize("foo42");
    ASSERT_EQ(t[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[0].text, "foo42");
})

TEST(lex_identifier_dollar_underscore, {
    auto t = tokenize("$_hello");
    ASSERT_EQ(t[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[0].text, "$_hello");
})

TEST(lex_keyword_true, {
    auto t = tokenize("true");
    ASSERT_EQ(t[0].type, TokenType::BOOLEAN);
    ASSERT_EQ(t[0].text, "true");
})

TEST(lex_keyword_false, {
    auto t = tokenize("false");
    ASSERT_EQ(t[0].type, TokenType::BOOLEAN);
})

TEST(lex_keyword_null, {
    auto t = tokenize("null");
    ASSERT_EQ(t[0].type, TokenType::NULL_LITERAL);
})

TEST(lex_keyword_undefined, {
    auto t = tokenize("undefined");
    ASSERT_EQ(t[0].type, TokenType::UNDEFINED);
})

TEST(lex_keyword_if_is_identifier, {
    auto t = tokenize("if");
    ASSERT_EQ(t[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[0].text, "if");
})

TEST(lex_keyword_return_is_identifier, {
    auto t = tokenize("return");
    ASSERT_EQ(t[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[0].text, "return");
})

// ---------------------------------------------------------------------------
// Operators / punctuators
// ---------------------------------------------------------------------------

TEST(lex_operators_single_char, {
    auto td = tokenize("1/2");
    ASSERT_EQ(td[1].type, TokenType::SLASH);
    auto t1 = tokenize("=");  ASSERT_EQ(t1[0].type, TokenType::EQUALS);
    auto t2 = tokenize("+");  ASSERT_EQ(t2[0].type, TokenType::PLUS);
    auto t3 = tokenize("-");  ASSERT_EQ(t3[0].type, TokenType::MINUS);
    auto t4 = tokenize("*");  ASSERT_EQ(t4[0].type, TokenType::STAR);
    auto t6 = tokenize("%");  ASSERT_EQ(t6[0].type, TokenType::PERCENT);
    auto t7 = tokenize("<");  ASSERT_EQ(t7[0].type, TokenType::LT);
    auto t8 = tokenize(">");  ASSERT_EQ(t8[0].type, TokenType::GT);
    auto t9 = tokenize("!");  ASSERT_EQ(t9[0].type, TokenType::NOT);
    auto t10 = tokenize("&"); ASSERT_EQ(t10[0].type, TokenType::AMPERSAND);
    auto t11 = tokenize("|"); ASSERT_EQ(t11[0].type, TokenType::PIPE);
    auto t12 = tokenize("^"); ASSERT_EQ(t12[0].type, TokenType::CARET);
    auto t13 = tokenize("~"); ASSERT_EQ(t13[0].type, TokenType::TILDE);
    auto t14 = tokenize("("); ASSERT_EQ(t14[0].type, TokenType::LPAREN);
    auto t15 = tokenize(")"); ASSERT_EQ(t15[0].type, TokenType::RPAREN);
    auto t16 = tokenize("."); ASSERT_EQ(t16[0].type, TokenType::DOT);
    auto t17 = tokenize(","); ASSERT_EQ(t17[0].type, TokenType::COMMA);
    auto t18 = tokenize(";"); ASSERT_EQ(t18[0].type, TokenType::SEMICOLON);
    auto t19 = tokenize(":"); ASSERT_EQ(t19[0].type, TokenType::COLON);
    auto t20 = tokenize("{"); ASSERT_EQ(t20[0].type, TokenType::LBRACE);
    auto t21 = tokenize("}"); ASSERT_EQ(t21[0].type, TokenType::RBRACE);
    auto t22 = tokenize("["); ASSERT_EQ(t22[0].type, TokenType::LBRACKET);
    auto t23 = tokenize("]"); ASSERT_EQ(t23[0].type, TokenType::RBRACKET);
    auto t24 = tokenize("?"); ASSERT_EQ(t24[0].type, TokenType::QUESTION);
})

TEST(lex_operators_multi_char, {
    ASSERT_EQ(tokenize("===")[0].type, TokenType::EQ_EQ_EQ);
    ASSERT_EQ(tokenize("!==")[0].type, TokenType::NOT_EQ_EQ);
    ASSERT_EQ(tokenize("==")[0].type, TokenType::EQ_EQ);
    ASSERT_EQ(tokenize("!=")[0].type, TokenType::NOT_EQ);
    ASSERT_EQ(tokenize("<=")[0].type, TokenType::LT_EQ);
    ASSERT_EQ(tokenize(">=")[0].type, TokenType::GT_EQ);
    ASSERT_EQ(tokenize("&&")[0].type, TokenType::AND_AND);
    ASSERT_EQ(tokenize("||")[0].type, TokenType::OR_OR);
    ASSERT_EQ(tokenize("++")[0].type, TokenType::PLUS_PLUS);
    ASSERT_EQ(tokenize("--")[0].type, TokenType::MINUS_MINUS);
    ASSERT_EQ(tokenize("+=")[0].type, TokenType::PLUS_EQ);
    ASSERT_EQ(tokenize("-=")[0].type, TokenType::MINUS_EQ);
    ASSERT_EQ(tokenize("=>")[0].type, TokenType::ARROW);
    // Batch test for proper multi-char tokenization with spacing
    auto t = tokenize("=== !== == != <= >= && || ++ -- += -= => ?? ?.");
    ASSERT(t.size() == 16u);
})

TEST(lex_operators_coalesce, {
    auto t = tokenize("??");
    ASSERT_EQ(t[0].type, TokenType::NULLISH_COALESCING);
})

TEST(lex_operators_question_dot, {
    auto t = tokenize("?.");
    ASSERT_EQ(t[0].type, TokenType::QUESTION_DOT);
})

TEST(lex_operators_compound, {
    auto t = tokenize("/=  *=");
    ASSERT(t.size() == 3u);
    ASSERT_EQ(t[0].type, TokenType::SLASH_EQ);
    ASSERT_EQ(t[1].type, TokenType::STAR_EQ);
})

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

TEST(lex_template_simple, {
    auto t = tokenize("`hello`");
    ASSERT_EQ(t.size(), 2u);
    ASSERT_EQ(t[0].type, TokenType::TEMPLATE_TAIL);
    ASSERT_EQ(t[0].text, "hello");
})

TEST(lex_template_empty, {
    auto t = tokenize("``");
    ASSERT_EQ(t[0].type, TokenType::TEMPLATE_TAIL);
    ASSERT_EQ(t[0].text, "");
})

TEST(lex_template_with_interpolation, {
    auto t = tokenize("`hello ${name} world`");
    ASSERT(t.size() >= 3u);
    ASSERT_EQ(t[0].type, TokenType::TEMPLATE_HEAD);
    ASSERT_EQ(t[0].text, "hello ");
    ASSERT_EQ(t[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[1].text, "name");
    ASSERT_EQ(t[2].type, TokenType::TEMPLATE_TAIL);
    ASSERT_EQ(t[2].text, " world");
})

TEST(lex_template_interpolation_at_start, {
    auto t = tokenize("`${a} end`");
    ASSERT(t.size() >= 3u);
    ASSERT_EQ(t[0].type, TokenType::TEMPLATE_HEAD);
    ASSERT_EQ(t[0].text, "");
    ASSERT_EQ(t[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[1].text, "a");
    ASSERT_EQ(t[2].type, TokenType::TEMPLATE_TAIL);
    ASSERT_EQ(t[2].text, " end");
})

TEST(lex_template_interpolation_multiple, {
    auto t = tokenize("`${a} middle ${b} end`");
    ASSERT(t.size() >= 5u);
    ASSERT_EQ(t[0].type, TokenType::TEMPLATE_HEAD);
    ASSERT_EQ(t[0].text, "");
    ASSERT_EQ(t[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[1].text, "a");
    ASSERT_EQ(t[2].type, TokenType::TEMPLATE_MIDDLE);
    ASSERT_EQ(t[2].text, " middle ");
    ASSERT_EQ(t[3].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[3].text, "b");
    ASSERT_EQ(t[4].type, TokenType::TEMPLATE_TAIL);
    ASSERT_EQ(t[4].text, " end");
})

TEST(lex_template_interpolation_nested_braces, {
    auto t = tokenize("`${a + {x: 1}} end`");
    ASSERT(t.size() >= 9u);
    ASSERT_EQ(t[0].type, TokenType::TEMPLATE_HEAD);
    ASSERT_EQ(t[0].text, "");
    ASSERT_EQ(t[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[1].text, "a");
    ASSERT_EQ(t[2].type, TokenType::PLUS);
    ASSERT_EQ(t[3].type, TokenType::LBRACE);
    ASSERT_EQ(t[4].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[4].text, "x");
    ASSERT_EQ(t[5].type, TokenType::COLON);
    ASSERT_EQ(t[6].type, TokenType::NUMBER);
    ASSERT_EQ(t[6].text, "1");
    ASSERT_EQ(t[7].type, TokenType::RBRACE);
    ASSERT_EQ(t[8].type, TokenType::TEMPLATE_TAIL);
    ASSERT_EQ(t[8].text, " end");
})

// ---------------------------------------------------------------------------
// Regexp
// ---------------------------------------------------------------------------

TEST(lex_regexp_simple, {
    auto t = tokenize("(/abc/)");
    ASSERT(t.size() >= 4u);
    ASSERT_EQ(t[0].type, TokenType::LPAREN);
    ASSERT_EQ(t[1].type, TokenType::REGEXP);
    ASSERT_EQ(t[1].text, "/abc/");
    ASSERT_EQ(t[2].type, TokenType::RPAREN);
})

TEST(lex_regexp_with_flags, {
    auto t = tokenize("(/abc/gi)");
    ASSERT_EQ(t[1].type, TokenType::REGEXP);
    ASSERT_EQ(t[1].text, "/abc/gi");
})

TEST(lex_regexp_with_escape, {
    auto t = tokenize("(/a\\/b/)");
    ASSERT_EQ(t[1].type, TokenType::REGEXP);
    ASSERT_EQ(t[1].text, "/a\\/b/");
})

TEST(lex_regexp_with_char_class, {
    auto t = tokenize("(/[abc]/)");
    ASSERT_EQ(t[1].type, TokenType::REGEXP);
})

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------

TEST(lex_comment_line, {
    auto t = tokenize("42 // line comment\n 7");
    ASSERT_EQ(t.size(), 3u);
    ASSERT_EQ(t[0].numeric_value, 42.0);
    ASSERT_EQ(t[1].numeric_value, 7.0);
    ASSERT_EQ(t[2].type, TokenType::EOF_TOKEN);
})

TEST(lex_comment_block, {
    auto t = tokenize("42 /* block */ 7");
    ASSERT_EQ(t.size(), 3u);
    ASSERT_EQ(t[0].numeric_value, 42.0);
    ASSERT_EQ(t[1].numeric_value, 7.0);
})

TEST(lex_comment_block_multi_line, {
    auto t = tokenize("42 /* \n multi \n */ 7");
    ASSERT_EQ(t.size(), 3u);
    ASSERT_EQ(t[0].numeric_value, 42.0);
    ASSERT_EQ(t[1].numeric_value, 7.0);
})

// ---------------------------------------------------------------------------
// Empty / Whitespace
// ---------------------------------------------------------------------------

TEST(lex_empty, {
    auto t = tokenize("");
    ASSERT_EQ(t.size(), 1u);
    ASSERT_EQ(t[0].type, TokenType::EOF_TOKEN);
})

TEST(lex_whitespace_only, {
    auto t = tokenize("   \t\n  \r\n  ");
    ASSERT_EQ(t.size(), 1u);
    ASSERT_EQ(t[0].type, TokenType::EOF_TOKEN);
})

TEST(lex_whitespace_between_tokens, {
    auto t = tokenize(" 1 + 2 ");
    ASSERT_EQ(t.size(), 4u);
    ASSERT_EQ(t[0].numeric_value, 1.0);
    ASSERT_EQ(t[1].type, TokenType::PLUS);
    ASSERT_EQ(t[2].numeric_value, 2.0);
})

// ---------------------------------------------------------------------------
// peek / next interaction
// ---------------------------------------------------------------------------

TEST(lex_peek_next, {
    Lexer l("1 2 3");
    ASSERT(l.has_next());
    Token p1 = l.peek();
    ASSERT_EQ(p1.numeric_value, 1.0);
    Token p2 = l.peek();
    ASSERT_EQ(p2.numeric_value, 1.0);
    Token n1 = l.next();
    ASSERT_EQ(n1.numeric_value, 1.0);
    Token p3 = l.peek();
    ASSERT_EQ(p3.numeric_value, 2.0);
    Token n2 = l.next();
    ASSERT_EQ(n2.numeric_value, 2.0);
    Token n3 = l.next();
    ASSERT_EQ(n3.numeric_value, 3.0);
})

// ---------------------------------------------------------------------------
// Position tracking (line / column)
// ---------------------------------------------------------------------------

TEST(lex_position_basic, {
    Lexer l("42\nfoo");
    Token t1 = l.next();
    ASSERT_EQ(t1.line, 1u);
    ASSERT_EQ(t1.column, 1u);
    Token t2 = l.next();
    ASSERT_EQ(t2.line, 2u);
    ASSERT_EQ(t2.column, 1u);
})

TEST(lex_position_punctuator, {
    Lexer l("===\n+");
    Token t1 = l.next();
    ASSERT_EQ(t1.line, 1u);
    ASSERT_EQ(t1.column, 1u);
    ASSERT_EQ(t1.type, TokenType::EQ_EQ_EQ);
    Token t2 = l.next();
    ASSERT_EQ(t2.line, 2u);
    ASSERT_EQ(t2.column, 1u);
    ASSERT_EQ(t2.type, TokenType::PLUS);
})

TEST(lex_position_arrow, {
    // => is ARROW, not = plus >
    Lexer l("=>");
    Token t = l.next();
    ASSERT_EQ(t.type, TokenType::ARROW);
    ASSERT_EQ(t.column, 1u);
})

TEST(lex_not_arrow_on_minus_gt, {
    // JS has no -> operator, only =>
    auto t = tokenize("x->y");
    ASSERT(t.size() >= 4u);
    ASSERT_EQ(t[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[0].text, "x");
    ASSERT_EQ(t[1].type, TokenType::MINUS);
    ASSERT_EQ(t[2].type, TokenType::GT);
    ASSERT_EQ(t[3].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[3].text, "y");
})

TEST(lex_regexp_not_after_value, {
    // After a value literal, / is division, not regexp
    auto t = tokenize("true/2");
    ASSERT(t.size() >= 3u);
    ASSERT_EQ(t[0].type, TokenType::BOOLEAN);
    ASSERT_EQ(t[1].type, TokenType::SLASH);
    ASSERT_EQ(t[2].type, TokenType::NUMBER);
})

TEST(lex_octal_digit_validation, {
    // 0o78 should parse as 0o7 NUMBER and 8 NUMBER
    auto t = tokenize("0o78");
    ASSERT(t.size() >= 3u);
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].text, "0o7");
    ASSERT_EQ(t[1].type, TokenType::NUMBER);
    ASSERT_EQ(t[1].text, "8");
})

TEST(lex_binary_digit_validation, {
    // 0b12 should parse as 0b1 NUMBER and 2 NUMBER
    auto t = tokenize("0b12");
    ASSERT(t.size() >= 3u);
    ASSERT_EQ(t[0].type, TokenType::NUMBER);
    ASSERT_EQ(t[0].text, "0b1");
    ASSERT_EQ(t[1].type, TokenType::NUMBER);
    ASSERT_EQ(t[1].text, "2");
})

TEST(lex_regexp_after_return, {
    // return /regex/ — keyword permits regexp
    auto t = tokenize("return /foo/g");
    ASSERT(t.size() >= 3u);
    ASSERT_EQ(t[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[0].text, "return");
    ASSERT_EQ(t[1].type, TokenType::REGEXP);
})

TEST(lex_regexp_after_throw, {
    auto t = tokenize("throw /foo/");
    ASSERT_EQ(t[0].text, "throw");
    ASSERT_EQ(t[1].type, TokenType::REGEXP);
})

TEST(lex_regexp_after_lbrace, {
    auto t = tokenize("{/foo/}");
    ASSERT(t.size() >= 4u);
    ASSERT_EQ(t[0].type, TokenType::LBRACE);
    ASSERT_EQ(t[1].type, TokenType::REGEXP);
    ASSERT_EQ(t[2].type, TokenType::RBRACE);
})

TEST(lex_regexp_after_typeof, {
    auto t = tokenize("typeof /foo/");
    ASSERT_EQ(t[1].type, TokenType::REGEXP);
})

TEST(lex_string_newline_stops, {
    // A newline inside a string literal terminates it (syntax error)
    auto t = tokenize("\"a\nb\"");
    ASSERT(t.size() >= 3u);
    ASSERT_EQ(t[0].type, TokenType::STRING);
    ASSERT_EQ(t[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(t[1].text, "b");
})

TEST(lex_string_escape_x_two_nibbles, {
    // \x41 produces character 0x41 = 'A'
    auto t = tokenize("\"\\x41\"");
    ASSERT_EQ(t[0].text, "A");
})

TEST(lex_string_escape_x_single_nibble, {
    // \x4 with no second hex digit: high nibble=4, low nibble=0 => '@'
    auto t = tokenize("\"\\x4\"");
    ASSERT_EQ(t[0].text, "@");
})

TEST(lex_string_escape_x_no_overconsume, {
    // \x4z — z is not hex, so only 4 is consumed; z remains as literal
    auto t = tokenize("\"\\x4z\"");
    ASSERT_EQ(t[0].text, "@z");
})

// ---------------------------------------------------------------------------
// Phase 4: Prototype, new, instanceof, this binding
// ---------------------------------------------------------------------------

TEST(phase4_prototype_get_property, {
    VM vm;
    // Create proto object with 'x' = 42
    auto* proto_gc = vm.heap()->alloc_object();
    proto_gc->obj.set("x", JSValue::number(42));
    // Create child object with proto as prototype
    auto* child_gc = vm.heap()->alloc_object();
    child_gc->obj.prototype = JSValue::object(&proto_gc->obj);
    child_gc->obj.set("y", JSValue::number(7));
    // get_property should find 'x' via prototype chain
    JSValue x_val = child_gc->obj.get_property("x");
    ASSERT(x_val.type == JSValue::Type::NUMBER);
    ASSERT_EQ(x_val.number_val, 42);
    // get_property should find own 'y'
    JSValue y_val = child_gc->obj.get_property("y");
    ASSERT(y_val.type == JSValue::Type::NUMBER);
    ASSERT_EQ(y_val.number_val, 7);
    // set_property on child should not write to prototype
    child_gc->obj.set_property("x", JSValue::number(100));
    JSValue proto_x = proto_gc->obj.get_property("x");
    ASSERT_EQ(proto_x.number_val, 42);
    JSValue child_x = child_gc->obj.get_property("x");
    ASSERT_EQ(child_x.number_val, 100);
})

TEST(phase4_instanceof, {
    VM vm;
    auto* parent_proto = vm.heap()->alloc_object();
    parent_proto->obj.set("type", JSValue::string("parent"));
    auto* child_proto = vm.heap()->alloc_object();
    child_proto->obj.prototype = JSValue::object(&parent_proto->obj);
    auto* instance = vm.heap()->alloc_object();
    instance->obj.prototype = JSValue::object(&child_proto->obj);
    // instanceof: check if parent_proto is in instance's prototype chain
    bool result = JSObject::prototype_chain_contains(JSValue::object(&instance->obj), JSValue::object(&parent_proto->obj));
    ASSERT(result);
    // Check against unrelated proto
    auto* unrelated = vm.heap()->alloc_object();
    bool not_result = JSObject::prototype_chain_contains(JSValue::object(&instance->obj), JSValue::object(&unrelated->obj));
    ASSERT(!not_result);
})

TEST(phase4_new_operator, {
    VM vm;
    // Create a constructor function
    auto* ctor_fn = vm.create_native_fn(
        [](const std::vector<JSValue>&, void*) -> JSValue {
            return JSValue::undefined();
        }, true, nullptr);
    // Set prototype property on constructor
    auto* proto_gc = vm.heap()->alloc_object();
    proto_gc->obj.set("greet", JSValue::string("hello"));
    ctor_fn->prototype_property = JSValue::object(&proto_gc->obj);
    
    // Test instance creation via prototype_chain_contains
    auto* instance = vm.heap()->alloc_object();
    instance->obj.prototype = ctor_fn->prototype_property;
    bool in_chain = instance->obj.prototype_chain_contains(JSValue::object(&instance->obj), JSValue::object(&proto_gc->obj));
    ASSERT(in_chain);
})
