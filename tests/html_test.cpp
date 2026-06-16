#include "test_framework.hpp"
#include "utility.hpp"
#include "../html/token.hpp"
#include "../html/utf8.hpp"
#include "../html/tokenizer.hpp"
#include "../html/entities.hpp"
#include "../html/parser.hpp"
#include "../html/traversal.hpp"

TEST(token_types, {
    using namespace browser::html;
    DoctypeToken dt;
    dt.name = "html";
    Token t(dt);
    ASSERT_EQ(get_type(t), TokenType::DOCTYPE);
    ASSERT(!is_tag(t));
})

TEST(token_tag, {
    using namespace browser::html;
    TagToken tag;
    tag.type = TokenType::START_TAG;
    tag.tag_name = "div";
    Token t(tag);
    ASSERT_EQ(get_type(t), TokenType::START_TAG);
    ASSERT(is_tag(t, "div"));
    ASSERT(!is_tag(t, "span"));
})

TEST(token_end_tag, {
    using namespace browser::html;
    TagToken tag;
    tag.type = TokenType::END_TAG;
    tag.tag_name = "p";
    Token t(tag);
    ASSERT_EQ(get_type(t), TokenType::END_TAG);
    ASSERT(is_tag(t, "p"));
})

TEST(token_comment, {
    using namespace browser::html;
    CommentToken ct;
    ct.data = "hello";
    Token t(ct);
    ASSERT_EQ(get_type(t), TokenType::COMMENT);
})

TEST(token_character, {
    using namespace browser::html;
    CharacterToken ch;
    ch.character = 'A';
    Token t(ch);
    ASSERT_EQ(get_type(t), TokenType::CHARACTER);
})

TEST(token_eof, {
    using namespace browser::html;
    EOFToken eof;
    Token t(eof);
    ASSERT_EQ(get_type(t), TokenType::END_OF_FILE);
})

TEST(token_tag_attributes, {
    using namespace browser::html;
    TagToken tag;
    tag.type = TokenType::START_TAG;
    tag.tag_name = "a";
    browser::html::Attribute a1;
    a1.name = "href"; a1.value = "https://example.com";
    tag.attributes.push_back(a1);
    browser::html::Attribute a2;
    a2.name = "class"; a2.value = "link";
    tag.attributes.push_back(a2);
    ASSERT_EQ(tag.attributes.size(), 2u);
    ASSERT_EQ(tag.attributes[0].name, "href");
    ASSERT_EQ(tag.attributes[0].value, "https://example.com");
})

TEST(utf8_ascii, {
    auto r = browser::html::decode_utf8((const browser::u8*)"A", 1);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), static_cast<browser::u32>('A'));
    ASSERT_EQ(r.bytes_consumed, 1u);
})

TEST(utf8_2byte, {
    browser::u8 d[2];
    d[0] = 0xC2; d[1] = 0xA9;
    auto r = browser::html::decode_utf8(d, 2);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), 0xA9u);
    ASSERT_EQ(r.bytes_consumed, 2u);
})

TEST(utf8_3byte, {
    browser::u8 d[3];
    d[0] = 0xE4; d[1] = 0xB8; d[2] = 0x96;
    auto r = browser::html::decode_utf8(d, 3);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), 0x4E16u);
    ASSERT_EQ(r.bytes_consumed, 3u);
})

TEST(utf8_4byte, {
    browser::u8 d[4];
    d[0] = 0xF0; d[1] = 0x9F; d[2] = 0x98; d[3] = 0x80;
    auto r = browser::html::decode_utf8(d, 4);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), 0x1F600u);
    ASSERT_EQ(r.bytes_consumed, 4u);
})

TEST(utf8_overlong, {
    browser::u8 d[2];
    d[0] = 0xC1; d[1] = 0x81;
    auto r = browser::html::decode_utf8(d, 2);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), 0xFFFDu);
})

TEST(utf8_surrogate, {
    browser::u8 d[3];
    d[0] = 0xED; d[1] = 0xA0; d[2] = 0x80;
    auto r = browser::html::decode_utf8(d, 3);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), 0xFFFDu);
})

TEST(utf8_truncated, {
    browser::u8 d[1];
    d[0] = 0xE4;
    auto r = browser::html::decode_utf8(d, 1);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), 0xFFFDu);
})

TEST(utf8_invalid_continuation, {
    browser::u8 d[2];
    d[0] = 0xC2; d[1] = 0x00;
    auto r = browser::html::decode_utf8(d, 2);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), 0xFFFDu);
})

TEST(utf8_empty, {
    auto r = browser::html::decode_utf8((const browser::u8*)"", 0);
    ASSERT_EQ(static_cast<browser::u32>(r.codepoint), 0xFFFDu);
    ASSERT_EQ(r.bytes_consumed, 1u);
})

TEST(tokenizer_simple, {
    browser::html::Tokenizer tok;
    tok.feed("<html><body><p>Hello</p></body></html>");
    tok.end();
    ASSERT(tok.has_next());
    auto t1 = tok.next();
    ASSERT(browser::html::is_tag(t1, "html"));
    ASSERT_EQ(browser::html::get_type(t1), browser::html::TokenType::START_TAG);
    auto t2 = tok.next();
    ASSERT(browser::html::is_tag(t2, "body"));
    auto t3 = tok.next();
    ASSERT(browser::html::is_tag(t3, "p"));
    auto t4 = tok.next();
    ASSERT_EQ(browser::html::get_type(t4), browser::html::TokenType::CHARACTER);
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t4).character, U'H');
    auto t5 = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t5).character, U'e');
    auto t6 = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t6).character, U'l');
    auto t7 = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t7).character, U'l');
    auto t8 = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t8).character, U'o');
    auto t9 = tok.next();
    ASSERT(browser::html::is_tag(t9, "p"));
    ASSERT_EQ(browser::html::get_type(t9), browser::html::TokenType::END_TAG);
    auto t10 = tok.next();
    ASSERT(browser::html::is_tag(t10, "body"));
    ASSERT_EQ(browser::html::get_type(t10), browser::html::TokenType::END_TAG);
    auto t11 = tok.next();
    ASSERT(browser::html::is_tag(t11, "html"));
    ASSERT_EQ(browser::html::get_type(t11), browser::html::TokenType::END_TAG);
    auto t12 = tok.next();
    ASSERT_EQ(browser::html::get_type(t12), browser::html::TokenType::END_OF_FILE);
    ASSERT(!tok.has_next());
})

TEST(tokenizer_attributes, {
    browser::html::Tokenizer tok;
    tok.feed("<div class=\"main\" id=\"content\">");
    tok.end();
    ASSERT(tok.has_next());
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "div"));
    auto& tag = std::get<browser::html::TagToken>(t);
    ASSERT_EQ(tag.attributes.size(), 2u);
    ASSERT_EQ(tag.attributes[0].name, "class");
    ASSERT_EQ(tag.attributes[0].value, "main");
    ASSERT_EQ(tag.attributes[1].name, "id");
    ASSERT_EQ(tag.attributes[1].value, "content");
})

TEST(tokenizer_self_closing, {
    browser::html::Tokenizer tok;
    tok.feed("<br/>");
    tok.end();
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "br"));
    auto& tag = std::get<browser::html::TagToken>(t);
    ASSERT(tag.self_closing);
})

TEST(tokenizer_comment, {
    browser::html::Tokenizer tok;
    tok.feed("<!-- comment -->");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::COMMENT);
    ASSERT_EQ(std::get<browser::html::CommentToken>(t).data, "- comment ");
})

TEST(tokenizer_doctype, {
    browser::html::Tokenizer tok;
    tok.feed("<!DOCTYPE html>");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::DOCTYPE);
    auto& dt = std::get<browser::html::DoctypeToken>(t);
    ASSERT_EQ(dt.name, "html");
})

TEST(tokenizer_char_ref_named, {
    browser::html::Tokenizer tok;
    tok.feed("&amp;");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::CHARACTER);
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t).character, U'&');
})

TEST(tokenizer_char_ref_numeric, {
    browser::html::Tokenizer tok;
    tok.feed("&#x26;");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::CHARACTER);
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t).character, U'&');
})

TEST(tokenizer_eof, {
    browser::html::Tokenizer tok;
    tok.feed("<div>");
    tok.end();
    ASSERT(tok.has_next());
    tok.next();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::END_OF_FILE);
})

TEST(tokenizer_empty, {
    browser::html::Tokenizer tok;
    tok.end();
    ASSERT(tok.has_next());
})

TEST(tokenizer_single_attr_no_value, {
    browser::html::Tokenizer tok;
    tok.feed("<input disabled>");
    tok.end();
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "input"));
    auto& tag = std::get<browser::html::TagToken>(t);
    ASSERT_EQ(tag.attributes.size(), 1u);
    ASSERT_EQ(tag.attributes[0].name, "disabled");
    ASSERT_EQ(tag.attributes[0].value, "");
})

TEST(tokenizer_attr_single_quoted, {
    browser::html::Tokenizer tok;
    tok.feed("<div class='single'>");
    tok.end();
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "div"));
    auto& tag = std::get<browser::html::TagToken>(t);
    ASSERT_EQ(tag.attributes.size(), 1u);
    ASSERT_EQ(tag.attributes[0].name, "class");
    ASSERT_EQ(tag.attributes[0].value, "single");
})

TEST(tokenizer_attr_unquoted, {
    browser::html::Tokenizer tok;
    tok.feed("<div class=main>");
    tok.end();
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "div"));
    auto& tag = std::get<browser::html::TagToken>(t);
    ASSERT_EQ(tag.attributes.size(), 1u);
    ASSERT_EQ(tag.attributes[0].name, "class");
    ASSERT_EQ(tag.attributes[0].value, "main");
})

TEST(tokenizer_entity_table, {
    ASSERT(browser::html::HTML_ENTITIES_COUNT > 2000u);
    ASSERT_EQ(browser::html::HTML_ENTITIES[0].name[0], 'A');
})

TEST(tokenizer_bogus_comment, {
    browser::html::Tokenizer tok;
    tok.feed("<!not-a-comment>");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::COMMENT);
})

TEST(tokenizer_multi_attrs, {
    browser::html::Tokenizer tok;
    tok.feed("<a href=\"https://x.com\" class=\"link\" id=\"main\">");
    tok.end();
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "a"));
    auto& tag = std::get<browser::html::TagToken>(t);
    ASSERT_EQ(tag.attributes.size(), 3u);
    ASSERT_EQ(tag.attributes[0].name, "href");
    ASSERT_EQ(tag.attributes[1].name, "class");
    ASSERT_EQ(tag.attributes[2].name, "id");
    ASSERT_EQ(tag.attributes[0].value, "https://x.com");
})

TEST(tokenizer_tag_name_case, {
    browser::html::Tokenizer tok;
    tok.feed("<DIV>");
    tok.end();
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "div"));
})

TEST(tokenizer_attr_name_case, {
    browser::html::Tokenizer tok;
    tok.feed("<div CLASS=\"main\">");
    tok.end();
    auto t = tok.next();
    auto& tag = std::get<browser::html::TagToken>(t);
    ASSERT_EQ(tag.attributes.size(), 1u);
    ASSERT_EQ(tag.attributes[0].name, "class");
})

TEST(tokenizer_doctype_public, {
    browser::html::Tokenizer tok;
    tok.feed("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0//EN\">");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::DOCTYPE);
    auto& dt = std::get<browser::html::DoctypeToken>(t);
    ASSERT_EQ(dt.name, "html");
    ASSERT_EQ(dt.public_id, "-//W3C//DTD XHTML 1.0//EN");
})

TEST(tokenizer_doctype_system, {
    browser::html::Tokenizer tok;
    tok.feed("<!DOCTYPE html SYSTEM \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::DOCTYPE);
    auto& dt = std::get<browser::html::DoctypeToken>(t);
    ASSERT_EQ(dt.name, "html");
    ASSERT_EQ(dt.system_id, "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd");
})

TEST(tokenizer_doctype_public_system, {
    browser::html::Tokenizer tok;
    tok.feed("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">");
    tok.end();
    auto t = tok.next();
    auto& dt = std::get<browser::html::DoctypeToken>(t);
    ASSERT_EQ(dt.name, "html");
    ASSERT_EQ(dt.public_id, "-//W3C//DTD XHTML 1.0 Transitional//EN");
    ASSERT_EQ(dt.system_id, "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd");
})

TEST(tokenizer_doctype_no_name, {
    browser::html::Tokenizer tok;
    tok.feed("<!DOCTYPE >");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(browser::html::get_type(t), browser::html::TokenType::DOCTYPE);
    auto& dt = std::get<browser::html::DoctypeToken>(t);
    ASSERT(dt.force_quirks);
})

TEST(tokenizer_attr_char_ref, {
    browser::html::Tokenizer tok;
    tok.feed("<div class=\"hello&amp;world\">");
    tok.end();
    auto t = tok.next();
    auto& tag = std::get<browser::html::TagToken>(t);
    ASSERT_EQ(tag.attributes[0].value, "hello&world");
})

TEST(tokenizer_numeric_dec, {
    browser::html::Tokenizer tok;
    tok.feed("&#38;");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t).character, U'&');
})

TEST(tokenizer_multiple_char_refs, {
    browser::html::Tokenizer tok;
    tok.feed("&amp;&lt;&gt;");
    tok.end();
    auto t1 = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t1).character, U'&');
    auto t2 = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t2).character, U'<');
    auto t3 = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t3).character, U'>');
})

TEST(tokenizer_newlines, {
    browser::html::Tokenizer tok;
    tok.feed("<div>\n</div>");
    tok.end();
    tok.next();
    auto t = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t).character, U'\n');
    auto t2 = tok.next();
    ASSERT(browser::html::is_tag(t2, "div"));
})

TEST(tokenizer_crlf, {
    browser::html::Tokenizer tok;
    tok.feed("<div>\r\n</div>");
    tok.end();
    tok.next();
    auto t = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t).character, U'\n');
})

TEST(tokenizer_self_closing_space, {
    browser::html::Tokenizer tok;
    tok.feed("<br />");
    tok.end();
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "br"));
    ASSERT(std::get<browser::html::TagToken>(t).self_closing);
})

TEST(tokenizer_uppercase_doctype, {
    browser::html::Tokenizer tok;
    tok.feed("<!DOCTYPE HTML>");
    tok.end();
    auto t = tok.next();
    auto& dt = std::get<browser::html::DoctypeToken>(t);
    ASSERT_EQ(dt.name, "html");
})

TEST(tokenizer_partial_feed, {
    browser::html::Tokenizer tok;
    tok.feed("<ht");
    tok.feed("ml>");
    tok.end();
    auto t = tok.next();
    ASSERT(browser::html::is_tag(t, "html"));
})

TEST(tokenizer_char_ref_at_eof, {
    browser::html::Tokenizer tok;
    tok.feed("&amp");
    tok.end();
    auto t = tok.next();
    ASSERT_EQ(std::get<browser::html::CharacterToken>(t).character, U'&');
})

// --- Parser tests ---

TEST(dom_create, {
    using namespace browser::html;
    auto doc = create_document();
    auto html = create_element("html");
    auto body = create_element("body");
    auto text = create_text("Hello");
    append_child(body.get(), std::move(text));
    append_child(html.get(), std::move(body));
    append_child(doc.get(), std::move(html));
    ASSERT_EQ(doc->children.size(), 1u);
    ASSERT_EQ(doc->children[0]->type, browser::html::NodeType::ELEMENT);
})

TEST(parse_simple, {
    browser::html::Parser parser;
    auto doc = parser.parse("<html><body><p>Hello</p></body></html>");
    ASSERT(doc != nullptr);
    ASSERT(doc->children.size() > 0u);
})

TEST(parse_basic_dom, {
    browser::html::Parser parser;
    auto doc = parser.parse("<html><body><p>Hello</p></body></html>");
    ASSERT(doc->children.size() >= 1u);
    auto s = browser::html::serialize_dom(doc.get());
    ASSERT(s.find("<html>") != std::string::npos);
    ASSERT(s.find("<body>") != std::string::npos);
})

TEST(parse_paragraph, {
    browser::html::Parser parser;
    auto doc = parser.parse("<p>Hello</p>");
    auto* body = browser::html::find_element_by_tag(
        static_cast<browser::html::Element*>(doc->children[0].get()), "body");
    ASSERT(body != nullptr);
    auto* p = browser::html::find_element_by_tag(body, "p");
    ASSERT(p != nullptr);
    ASSERT_EQ(browser::html::inner_text(p), "Hello");
})

TEST(parse_nested, {
    browser::html::Parser parser;
    auto doc = parser.parse("<div><span>text</span></div>");
    auto* body = browser::html::find_element_by_tag(
        static_cast<browser::html::Element*>(doc->children[0].get()), "body");
    ASSERT(body != nullptr);
    auto* div = browser::html::find_element_by_tag(body, "div");
    ASSERT(div != nullptr);
    auto* span = browser::html::find_element_by_tag(div, "span");
    ASSERT(span != nullptr);
    ASSERT_EQ(browser::html::inner_text(span), "text");
})

TEST(parse_attributes, {
    browser::html::Parser parser;
    auto doc = parser.parse("<div id=\"main\" class=\"content\">");
    auto* body = browser::html::find_element_by_tag(
        static_cast<browser::html::Element*>(doc->children[0].get()), "body");
    ASSERT(body != nullptr);
    auto* div = browser::html::find_element_by_tag(body, "div");
    ASSERT(div != nullptr);
    ASSERT_EQ(div->get_attribute("id"), "main");
    ASSERT_EQ(div->get_attribute("class"), "content");
})

TEST(parse_heading, {
    browser::html::Parser parser;
    auto doc = parser.parse("<h1>Title</h1>");
    auto* body = browser::html::find_element_by_tag(
        static_cast<browser::html::Element*>(doc->children[0].get()), "body");
    ASSERT(body != nullptr);
    auto* h1 = browser::html::find_element_by_tag(body, "h1");
    ASSERT(h1 != nullptr);
    ASSERT_EQ(browser::html::inner_text(h1), "Title");
})

TEST(parse_list, {
    browser::html::Parser parser;
    auto doc = parser.parse("<ul><li>A</li><li>B</li></ul>");
    auto* body = browser::html::find_element_by_tag(
        static_cast<browser::html::Element*>(doc->children[0].get()), "body");
    ASSERT(body != nullptr);
    auto* ul = browser::html::find_element_by_tag(body, "ul");
    ASSERT(ul != nullptr);
    ASSERT_EQ(ul->children.size(), 2u);
})

TEST(parse_comment, {
    browser::html::Parser parser;
    auto doc = parser.parse("<p>Hello<!-- comment -->World</p>");
    auto* body = browser::html::find_element_by_tag(
        static_cast<browser::html::Element*>(doc->children[0].get()), "body");
    ASSERT(body != nullptr);
    auto* p = browser::html::find_element_by_tag(body, "p");
    ASSERT(p != nullptr);
    ASSERT_EQ(browser::html::inner_text(p), "HelloWorld");
})

TEST(parse_self_closing, {
    browser::html::Parser parser;
    auto doc = parser.parse("<br>");
    auto* body = browser::html::find_element_by_tag(
        static_cast<browser::html::Element*>(doc->children[0].get()), "body");
    ASSERT(body != nullptr);
    auto* br = browser::html::find_element_by_tag(body, "br");
    ASSERT(br != nullptr);
})

TEST(parse_empty, {
    browser::html::Parser parser;
    auto doc = parser.parse("");
    ASSERT(doc != nullptr);
    ASSERT(doc->children.size() >= 1u);
})

TEST(parse_implied_structure, {
    browser::html::Parser parser;
    auto doc = parser.parse("<p>Hello");
    ASSERT(doc->children.size() >= 1u);
    ASSERT(doc->children[0]->type == browser::html::NodeType::ELEMENT);
})

TEST(serialize_simple, {
    browser::html::Parser parser;
    auto doc = parser.parse("<html><body><p>Hello</p></body></html>");
    auto s = browser::html::serialize_dom(doc.get());
    ASSERT(!s.empty());
})

TEST(parse_doctype, {
    browser::html::Parser parser;
    auto doc = parser.parse("<!DOCTYPE html><html><body><p>Hi</p></body></html>");
    ASSERT(doc->children.size() >= 2u);
    ASSERT(doc->children[0]->type == browser::html::NodeType::DOCUMENT_TYPE);
})

TEST(parse_debug_serialize, {
    browser::html::Parser parser;
    auto doc = parser.parse("<p>Hello</p>");
    auto s = browser::html::serialize_dom(doc.get());
    // Serialization should at least contain tags
    ASSERT(s.find("<html>") != std::string::npos);
})
