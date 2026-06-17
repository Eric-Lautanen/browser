#include "test_framework.hpp"
#include "utility.hpp"
#include "../css/css_values.hpp"
#include "../css/tokenizer.hpp"
#include "../css/parser.hpp"
#include "../css/cascade/engine.hpp"
#include "../css/layout.hpp"

TEST(css_color_hex, {
    using namespace browser::css;
    auto c = Color::from_hex("#ff8800");
    ASSERT_EQ(static_cast<int>(c.r), 255);
    ASSERT_EQ(static_cast<int>(c.g), 136);
    ASSERT_EQ(static_cast<int>(c.b), 0);
    ASSERT_EQ(static_cast<int>(c.a), 255);
})

TEST(css_color_hex_3, {
    using namespace browser::css;
    auto c = Color::from_hex("#f80");
    ASSERT_EQ(static_cast<int>(c.r), 255);
    ASSERT_EQ(static_cast<int>(c.g), 136);
    ASSERT_EQ(static_cast<int>(c.b), 0);
})

TEST(css_color_hex_8, {
    using namespace browser::css;
    auto c = Color::from_hex("#ff880080");
    ASSERT_EQ(static_cast<int>(c.r), 255);
    ASSERT_EQ(static_cast<int>(c.g), 136);
    ASSERT_EQ(static_cast<int>(c.b), 0);
    ASSERT_EQ(static_cast<int>(c.a), 128);
})

TEST(css_color_name, {
    using namespace browser::css;
    auto c = Color::from_name("red");
    ASSERT_EQ(static_cast<int>(c.r), 255);
    ASSERT_EQ(static_cast<int>(c.g), 0);
    ASSERT_EQ(static_cast<int>(c.b), 0);
})

TEST(css_color_rgba, {
    using namespace browser::css;
    auto c = Color::from_rgba(10, 20, 30, 200);
    ASSERT_EQ(static_cast<int>(c.r), 10);
    ASSERT_EQ(static_cast<int>(c.g), 20);
    ASSERT_EQ(static_cast<int>(c.b), 30);
    ASSERT_EQ(static_cast<int>(c.a), 200);
})

TEST(css_tokenize_ident, {
    using namespace browser::css;
    CssTokenizer tok("body");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::IDENT);
    ASSERT_EQ(t.text, "body");
})

TEST(css_tokenize_hash, {
    using namespace browser::css;
    CssTokenizer tok("#bar");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::HASH);
    ASSERT_EQ(t.text, "bar");
})

TEST(css_tokenize_string_dq, {
    using namespace browser::css;
    CssTokenizer tok("\"hello\"");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::STRING);
    ASSERT_EQ(t.text, "hello");
})

TEST(css_tokenize_string_sq, {
    using namespace browser::css;
    CssTokenizer tok("'world'");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::STRING);
    ASSERT_EQ(t.text, "world");
})

TEST(css_tokenize_number_int, {
    using namespace browser::css;
    CssTokenizer tok("42");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::NUMBER);
    ASSERT_EQ(static_cast<int>(t.numeric_value), 42);
})

TEST(css_tokenize_number_float, {
    using namespace browser::css;
    CssTokenizer tok("3.14");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::NUMBER);
    ASSERT(t.numeric_value > 3.13f && t.numeric_value < 3.15f);
})

TEST(css_tokenize_number_dot, {
    using namespace browser::css;
    CssTokenizer tok(".5");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::NUMBER);
    ASSERT(t.numeric_value > 0.49f && t.numeric_value < 0.51f);
})

TEST(css_tokenize_number_exp, {
    using namespace browser::css;
    CssTokenizer tok("1e2");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::NUMBER);
    ASSERT(t.numeric_value > 99.9f && t.numeric_value < 100.1f);
})

TEST(css_tokenize_dimension, {
    using namespace browser::css;
    CssTokenizer tok("16px");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::DIMENSION);
    ASSERT(static_cast<int>(t.numeric_value) == 16);
    ASSERT_EQ(t.text, "px");
})

TEST(css_tokenize_percentage, {
    using namespace browser::css;
    CssTokenizer tok("50%");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::PERCENTAGE);
    ASSERT(static_cast<int>(t.numeric_value) == 50);
})

TEST(css_tokenize_function, {
    using namespace browser::css;
    CssTokenizer tok("rgb(255,0,0)");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::FUNCTION);
    ASSERT_EQ(t.text, "rgb");
})

TEST(css_tokenize_at_keyword, {
    using namespace browser::css;
    CssTokenizer tok("@media");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::AT_KEYWORD);
    ASSERT_EQ(t.text, "media");
})

TEST(css_tokenize_whitespace, {
    using namespace browser::css;
    CssTokenizer tok("  \n\t");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::WHITESPACE);
})

TEST(css_tokenize_colon, {
    using namespace browser::css;
    CssTokenizer tok(":");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::COLON);
})

TEST(css_tokenize_semicolon, {
    using namespace browser::css;
    CssTokenizer tok(";");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::SEMICOLON);
})

TEST(css_tokenize_comma, {
    using namespace browser::css;
    CssTokenizer tok(",");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::COMMA);
})

TEST(css_tokenize_brace, {
    using namespace browser::css;
    CssTokenizer tok("{");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::OPEN_BRACE);
})

TEST(css_tokenize_paren, {
    using namespace browser::css;
    CssTokenizer tok(")");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::CLOSE_PAREN);
})

TEST(css_tokenize_bracket, {
    using namespace browser::css;
    CssTokenizer tok("]");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::CLOSE_BRACKET);
})

TEST(css_tokenize_comment, {
    using namespace browser::css;
    CssTokenizer tok("/* comment */body");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::IDENT);
    ASSERT_EQ(t.text, "body");
})

TEST(css_tokenize_url, {
    using namespace browser::css;
    CssTokenizer tok("url(http://example.com)");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::URL);
    ASSERT_EQ(t.text, "http://example.com");
})

TEST(css_tokenize_eof, {
    using namespace browser::css;
    CssTokenizer tok("");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::EOF_TOKEN);
})

TEST(css_parse_simple, {
    using namespace browser::css;
    CssParser p("h1 { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds.size(), 1u);
    ASSERT_EQ(sheet.rules[0].declarations.size(), 1u);
    ASSERT_EQ(sheet.rules[0].declarations[0].property, "color");
})

TEST(css_parse_multi_selector, {
    using namespace browser::css;
    CssParser p("h1, h2 { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors.size(), 2u);
})

TEST(css_parse_nested, {
    using namespace browser::css;
    CssParser p("div p { color: blue; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].selectors.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds.size(), 2u);
})

TEST(css_parse_dimension, {
    using namespace browser::css;
    CssParser p("h1 { font-size: 16px; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].declarations.size(), 1u);
    ASSERT_EQ(sheet.rules[0].declarations[0].values.size(), 1u);
    ASSERT_EQ(sheet.rules[0].declarations[0].values[0].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(sheet.rules[0].declarations[0].values[0].length.value) == 16);
})

TEST(css_parse_important, {
    using namespace browser::css;
    CssParser p("h1 { color: red !important; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].declarations.size(), 1u);
    ASSERT(sheet.rules[0].declarations[0].important);
})

TEST(css_at_media, {
    using namespace browser::css;
    CssParser p("@media screen { body { color: red; } }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.at_rules.size(), 1u);
    ASSERT_EQ(sheet.at_rules[0].name, "media");
    ASSERT_EQ(sheet.at_rules[0].prelude, "screen");
    ASSERT_EQ(sheet.at_rules[0].rules.size(), 1u);
    ASSERT_EQ(sheet.at_rules[0].rules[0].selectors.size(), 1u);
})

TEST(css_parse_class_selector, {
    using namespace browser::css;
    CssParser p(".foo { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].type, SimpleSelector::Type::CLASS);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].name, "foo");
})

TEST(css_parse_id_selector, {
    using namespace browser::css;
    CssParser p("#bar { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].type, SimpleSelector::Type::ID);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].name, "bar");
})

TEST(css_tokenize_delim, {
    using namespace browser::css;
    CssTokenizer tok("~");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::DELIM);
    ASSERT_EQ(t.text, "~");
})

TEST(css_tokenize_adjacent, {
    using namespace browser::css;
    CssTokenizer tok("h1 + h2");
    auto t1 = tok.next();
    ASSERT_EQ(t1.type, CssTokenType::IDENT);
    auto t2 = tok.next();
    ASSERT_EQ(t2.type, CssTokenType::WHITESPACE);
    auto t3 = tok.next();
    ASSERT_EQ(t3.type, CssTokenType::DELIM);
    ASSERT_EQ(t3.text, "+");
    auto t4 = tok.next();
    ASSERT_EQ(t4.type, CssTokenType::WHITESPACE);
    auto t5 = tok.next();
    ASSERT_EQ(t5.type, CssTokenType::IDENT);
})

TEST(css_tokenize_positive_dot_number, {
    using namespace browser::css;
    CssTokenizer tok("+.5");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::NUMBER);
    ASSERT(t.numeric_value > 0.49f && t.numeric_value < 0.51f);
})

TEST(css_tokenize_negative_dot_number, {
    using namespace browser::css;
    CssTokenizer tok("-.5");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::NUMBER);
    ASSERT(t.numeric_value > -0.51f && t.numeric_value < -0.49f);
})

TEST(css_tokenize_negative_int, {
    using namespace browser::css;
    CssTokenizer tok("-42");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::NUMBER);
    ASSERT_EQ(static_cast<int>(t.numeric_value), -42);
})

TEST(css_tokenize_negative_dimension, {
    using namespace browser::css;
    CssTokenizer tok("-16px");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::DIMENSION);
    ASSERT(static_cast<int>(t.numeric_value) == -16);
    ASSERT_EQ(t.text, "px");
})

TEST(css_tokenize_negative_exp, {
    using namespace browser::css;
    CssTokenizer tok("1.5e-3");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::NUMBER);
    ASSERT(t.numeric_value > 0.0014f && t.numeric_value < 0.0016f);
})

TEST(css_tokenize_url_quoted, {
    using namespace browser::css;
    CssTokenizer tok("url(\"http://example.com\")");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::URL);
    ASSERT_EQ(t.text, "http://example.com");
})

TEST(css_tokenize_cdo, {
    using namespace browser::css;
    CssTokenizer tok("<!--");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::WHITESPACE);
})

TEST(css_tokenize_cdc, {
    using namespace browser::css;
    CssTokenizer tok("-->");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::WHITESPACE);
})

TEST(css_tokenize_cdo_then_ident, {
    using namespace browser::css;
    CssTokenizer tok("<!--body");
    auto t1 = tok.next();
    ASSERT_EQ(t1.type, CssTokenType::WHITESPACE);
    auto t2 = tok.next();
    ASSERT_EQ(t2.type, CssTokenType::IDENT);
    ASSERT_EQ(t2.text, "body");
})

TEST(css_tokenize_cdc_then_ident, {
    using namespace browser::css;
    CssTokenizer tok("--> body");
    auto t1 = tok.next();
    ASSERT_EQ(t1.type, CssTokenType::WHITESPACE);
    auto t2 = tok.next();
    ASSERT_EQ(t2.type, CssTokenType::WHITESPACE);
    auto t3 = tok.next();
    ASSERT_EQ(t3.type, CssTokenType::IDENT);
    ASSERT_EQ(t3.text, "body");
})

TEST(css_tokenize_multiple_comments, {
    using namespace browser::css;
    CssTokenizer tok("/* a *//* b */body");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::IDENT);
    ASSERT_EQ(t.text, "body");
})

TEST(css_tokenize_empty_input, {
    using namespace browser::css;
    CssTokenizer tok("");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::EOF_TOKEN);
})

TEST(css_tokenize_only_whitespace, {
    using namespace browser::css;
    CssTokenizer tok("   \n  ");
    auto t = tok.next();
    ASSERT_EQ(t.type, CssTokenType::WHITESPACE);
    auto t2 = tok.next();
    ASSERT_EQ(t2.type, CssTokenType::EOF_TOKEN);
})

TEST(css_parse_empty_stylesheet, {
    using namespace browser::css;
    CssParser p("");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules.size(), 0u);
    ASSERT_EQ(sheet.at_rules.size(), 0u);
})

TEST(css_parse_multiple_rules, {
    using namespace browser::css;
    CssParser p("h1 { color: red; } p { font-size: 12px; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules.size(), 2u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].name, "h1");
    ASSERT_EQ(sheet.rules[1].selectors[0].compounds[0].simples[0].name, "p");
})

TEST(css_parse_multiple_declarations, {
    using namespace browser::css;
    CssParser p("h1 { color: red; font-size: 16px; background: blue; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].declarations.size(), 3u);
    ASSERT_EQ(sheet.rules[0].declarations[0].property, "color");
    ASSERT_EQ(sheet.rules[0].declarations[1].property, "font-size");
    ASSERT_EQ(sheet.rules[0].declarations[2].property, "background");
})

TEST(css_parse_important_space_before, {
    using namespace browser::css;
    CssParser p("h1 { color: red  !important; }");
    auto sheet = p.parse();
    ASSERT(sheet.rules[0].declarations[0].important);
})

TEST(css_parse_child_combinator, {
    using namespace browser::css;
    CssParser p("div > p { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds.size(), 2u);
    ASSERT_EQ(sheet.rules[0].selectors[0].combinators.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].combinators[0], Combinator::CHILD);
})

TEST(css_parse_sibling_combinator, {
    using namespace browser::css;
    CssParser p("h1 + h2 { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].selectors[0].combinators.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].combinators[0], Combinator::ADJACENT_SIBLING);
})

TEST(css_parse_general_sibling, {
    using namespace browser::css;
    CssParser p("h1 ~ h2 { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].selectors[0].combinators.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].combinators[0], Combinator::GENERAL_SIBLING);
})

TEST(css_parse_attribute_selector, {
    using namespace browser::css;
    CssParser p("[disabled] { color: gray; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].type, SimpleSelector::Type::ATTRIBUTE);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].name, "disabled");
})

TEST(css_parse_pseudo_class, {
    using namespace browser::css;
    CssParser p("a:hover { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples.size(), 2u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[1].type, SimpleSelector::Type::PSEUDO_CLASS);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[1].name, "hover");
})

TEST(css_parse_universal_selector, {
    using namespace browser::css;
    CssParser p("* { margin: 0; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].type, SimpleSelector::Type::UNIVERSAL);
})

TEST(css_parse_hex_color_value, {
    using namespace browser::css;
    CssParser p("h1 { color: #ff8800; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].declarations[0].values.size(), 1u);
    ASSERT_EQ(sheet.rules[0].declarations[0].values[0].type, CSSValue::Type::COLOR);
    ASSERT_EQ(static_cast<int>(sheet.rules[0].declarations[0].values[0].color.r), 255);
    ASSERT_EQ(static_cast<int>(sheet.rules[0].declarations[0].values[0].color.g), 136);
})

TEST(css_parse_at_import, {
    using namespace browser::css;
    CssParser p("@import url(style.css);");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.at_rules.size(), 1u);
    ASSERT_EQ(sheet.at_rules[0].name, "import");
})

TEST(css_parse_string_value, {
    using namespace browser::css;
    CssParser p("h1::before { content: \"hello\"; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].declarations[0].values.size(), 1u);
    ASSERT_EQ(sheet.rules[0].declarations[0].values[0].type, CSSValue::Type::STRING);
    ASSERT_EQ(sheet.rules[0].declarations[0].values[0].string_value, "hello");
})

TEST(css_parse_url_value, {
    using namespace browser::css;
    CssParser p("h1 { background: url(bg.png); }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].declarations[0].values.size(), 1u);
    ASSERT_EQ(sheet.rules[0].declarations[0].values[0].type, CSSValue::Type::URL);
    ASSERT_EQ(sheet.rules[0].declarations[0].values[0].string_value, "bg.png");
})

TEST(css_parse_percentage_value, {
    using namespace browser::css;
    CssParser p("h1 { width: 50%; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules[0].declarations[0].values.size(), 1u);
    ASSERT_EQ(sheet.rules[0].declarations[0].values[0].type, CSSValue::Type::PERCENTAGE);
    ASSERT(static_cast<int>(sheet.rules[0].declarations[0].values[0].number) == 50);
})

TEST(css_color_transparent, {
    using namespace browser::css;
    auto c = Color::from_name("transparent");
    ASSERT_EQ(static_cast<int>(c.r), 0);
    ASSERT_EQ(static_cast<int>(c.g), 0);
    ASSERT_EQ(static_cast<int>(c.b), 0);
    ASSERT_EQ(static_cast<int>(c.a), 0);
})

TEST(css_color_orange, {
    using namespace browser::css;
    auto c = Color::from_name("orange");
    ASSERT_EQ(static_cast<int>(c.r), 255);
    ASSERT_EQ(static_cast<int>(c.g), 165);
    ASSERT_EQ(static_cast<int>(c.b), 0);
})

TEST(css_parse_nested_atrule, {
    using namespace browser::css;
    CssParser p("@media screen { @supports (display: flex) { body { color: red; } } }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.at_rules.size(), 1u);
    ASSERT_EQ(sheet.at_rules[0].name, "media");
    ASSERT_EQ(sheet.at_rules[0].at_rules.size(), 1u);
    ASSERT_EQ(sheet.at_rules[0].at_rules[0].name, "supports");
    ASSERT_EQ(sheet.at_rules[0].at_rules[0].rules.size(), 1u);
})

TEST(css_parse_multi_selector_compound, {
    using namespace browser::css;
    CssParser p("div.foo > p.bar { color: red; }");
    auto sheet = p.parse();
    ASSERT_EQ(sheet.rules.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds.size(), 2u);
    ASSERT_EQ(sheet.rules[0].selectors[0].combinators.size(), 1u);
    ASSERT_EQ(sheet.rules[0].selectors[0].combinators[0], Combinator::CHILD);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples.size(), 2u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].type, SimpleSelector::Type::TAG);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[0].name, "div");
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[1].type, SimpleSelector::Type::CLASS);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[0].simples[1].name, "foo");
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[1].simples.size(), 2u);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[1].simples[0].type, SimpleSelector::Type::TAG);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[1].simples[0].name, "p");
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[1].simples[1].type, SimpleSelector::Type::CLASS);
    ASSERT_EQ(sheet.rules[0].selectors[0].compounds[1].simples[1].name, "bar");
})

TEST(cascade_important_ordering, {
    using namespace browser::css;
    // Verify sort_matched_decls orders: UA normal < Author normal < Inline normal
    // < Author !important < Inline !important < UA !important

    Declaration decl_norm, decl_imp;
    decl_norm.property = "color";
    decl_norm.values.push_back({});
    decl_norm.values[0].type = CSSValue::Type::KEYWORD;
    decl_norm.values[0].keyword = "red";
    decl_imp = decl_norm;
    decl_imp.important = true;

    auto make_decl = [&](browser::u8 origin, bool important,
                         browser::u32 specificity_val, browser::u32 source_order) {
        MatchedDecl md;
        md.decl = important ? &decl_imp : &decl_norm;
        md.origin = origin;
        md.specificity.bits = specificity_val;
        md.source_order = source_order;
        return md;
    };

    // Build in reverse order to test sorting
    std::vector<MatchedDecl> decls;
    decls.push_back(make_decl(1, false, 100, 0));   // Author normal, high specificity
    decls.push_back(make_decl(0, false, 10, 1));    // UA normal
    decls.push_back(make_decl(1, true, 0, 2));      // Author !important
    decls.push_back(make_decl(2, false, 0, 3));     // Inline normal
    decls.push_back(make_decl(0, true, 0, 4));      // UA !important

    sort_matched_decls(decls);

    // Expected: UA norm < Author norm < Inline norm < Author !imp < UA !imp
    ASSERT_EQ(decls[0].origin, 0u);
    ASSERT_EQ(decls[0].decl->important, false);
    ASSERT_EQ(decls[1].origin, 1u);
    ASSERT_EQ(decls[1].decl->important, false);
    ASSERT_EQ(decls[2].origin, 2u);
    ASSERT_EQ(decls[2].decl->important, false);
    ASSERT_EQ(decls[3].origin, 1u);
    ASSERT_EQ(decls[3].decl->important, true);
    ASSERT_EQ(decls[4].origin, 0u);
    ASSERT_EQ(decls[4].decl->important, true);
})

TEST(cascade_inline_vs_author_important, {
    using namespace browser::css;
    // <div style="color:red"> vs div#x { color: blue !important; }
    // Author !important should beat inline normal (later in sorted vector wins)

    Declaration decl_inline, decl_author_imp;
    decl_inline.property = "color";
    decl_inline.values.push_back({});
    decl_inline.values[0].type = CSSValue::Type::KEYWORD;
    decl_inline.values[0].keyword = "red";
    decl_author_imp = decl_inline;
    decl_author_imp.values[0].keyword = "blue";
    decl_author_imp.important = true;

    std::vector<MatchedDecl> decls;
    MatchedDecl md_inline;
    md_inline.decl = &decl_inline;
    md_inline.origin = 2;
    md_inline.specificity.bits = 0;
    md_inline.source_order = 0;

    MatchedDecl md_author_imp;
    md_author_imp.decl = &decl_author_imp;
    md_author_imp.origin = 1;
    md_author_imp.specificity.bits = 200;
    md_author_imp.source_order = 1;

    decls.push_back(md_inline);
    decls.push_back(md_author_imp);

    sort_matched_decls(decls);

    // Should be: inline normal (origin 2) first, then author !important (origin 1)
    ASSERT_EQ(decls[0].origin, 2u);
    ASSERT_EQ(decls[0].decl->important, false);
    ASSERT_EQ(decls[1].origin, 1u);
    ASSERT_EQ(decls[1].decl->important, true);
})

TEST(table_row_group_ua_display, {
    using namespace browser::css;
    // Verify the UA stylesheet assigns correct display values to table elements
    CssParser ua_parser(R"(
tr { display: table-row; }
thead { display: table-header-group; }
tbody { display: table-row-group; }
tfoot { display: table-footer-group; }
)");
    StyleSheet ua = ua_parser.parse();
    auto find_display = [&](const std::string &tag) -> std::string {
        for (auto &rule : ua.rules) {
            for (auto &sel : rule.selectors) {
                for (auto &comp : sel.compounds) {
                    for (auto &ss : comp.simples) {
                        if (ss.type == SimpleSelector::Type::TAG && ss.name == tag) {
                            for (auto &d : rule.declarations) {
                                if (d.property == "display" && !d.values.empty())
                                    return d.values[0].keyword;
                            }
                        }
                    }
                }
            }
        }
        return "";
    };
    ASSERT_EQ(find_display("tr"), "table-row");
    ASSERT_EQ(find_display("thead"), "table-header-group");
    ASSERT_EQ(find_display("tbody"), "table-row-group");
    ASSERT_EQ(find_display("tfoot"), "table-footer-group");
})
