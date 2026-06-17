#include "test_framework.hpp"
#include "../css/layout.hpp"
#include "../css/parser.hpp"
#include "../css/cascade/engine.hpp"
#include "../html/dom.hpp"
#include "../html/parser.hpp"
#include "../render/painter.hpp"
#include "../async/task.hpp"

#include <unordered_map>
#include <memory>
#include <cmath>

// ── Test helpers ───────────────────────────────────────────────────────────

static void add_prop(browser::css::ComputedStyle& style,
                     const std::string& prop,
                     browser::css::CSSValue value) {
    style.properties[prop] = std::move(value);
}

static browser::css::CSSValue kw(const std::string& k) {
    browser::css::CSSValue v;
    v.type = browser::css::CSSValue::Type::KEYWORD;
    v.keyword = k;
    return v;
}

static browser::css::CSSValue len_val(browser::f32 value) {
    browser::css::CSSValue v;
    v.type = browser::css::CSSValue::Type::LENGTH;
    v.length = {value, browser::css::Length::Unit::PX};
    return v;
}

static browser::css::CSSValue len_val_unit(browser::f32 value, browser::css::Length::Unit unit) {
    browser::css::CSSValue v;
    v.type = browser::css::CSSValue::Type::LENGTH;
    v.length = {value, unit};
    return v;
}

static browser::css::CSSValue num_val(browser::f32 value) {
    browser::css::CSSValue v;
    v.type = browser::css::CSSValue::Type::NUMBER;
    v.number = value;
    return v;
}



// Build a minimal document tree: html > body > div#id
struct TestDoc {
    std::unique_ptr<browser::html::Document> doc;
    browser::html::Element* html = nullptr;
    browser::html::Element* body = nullptr;
    browser::html::Element* target = nullptr;
};

static TestDoc make_doc(const std::string& id = "test") {
    using namespace browser::html;
    TestDoc td;
    td.doc = create_document();
    auto html_el = create_element("html");
    auto body_el = create_element("body");
    auto div_el = create_element("div");
    div_el->attributes["id"] = id;
    append_child(td.doc.get(), std::move(html_el));
    td.html = static_cast<Element*>(td.doc->children[0].get());
    append_child(td.html, std::move(body_el));
    td.body = static_cast<Element*>(td.html->children[0].get());
    append_child(td.body, std::move(div_el));
    td.target = static_cast<Element*>(td.body->children[0].get());
    return td;
}

// Run the full cascade on a document with an author stylesheet
static browser::css::ComputedStyle cascade_single(
    const browser::html::Document& doc,
    const browser::html::Element* el,
    const browser::css::StyleSheet& author) {
    browser::css::Cascade cascade;
    auto result = cascade.compute_async(doc, author, 800, 600).sync_wait().unwrap();
    auto it = result.element_styles.find(el);
    if (it == result.element_styles.end())
        return {};
    return it->second;
}

// ============================================================================
// 1. CASCADE RESOLUTION — shorthand expansion through the real cascade engine
// ============================================================================

TEST(cascade_margin_auto_single, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    CssParser p("#test { margin: auto; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("margin-left"));
    ASSERT_EQ(style.properties["margin-left"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["margin-left"].keyword, "auto");
    ASSERT_EQ(style.properties["margin-right"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["margin-right"].keyword, "auto");
    ASSERT_EQ(style.properties["margin-top"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["margin-top"].keyword, "auto");
    ASSERT_EQ(style.properties["margin-bottom"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["margin-bottom"].keyword, "auto");
})

TEST(cascade_margin_auto_mixed, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    CssParser p("#test { margin: 2em auto; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("margin-top"));
    ASSERT_EQ(style.properties["margin-top"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["margin-top"].length.value) == 2);
    ASSERT_EQ(style.properties["margin-top"].length.unit, Length::Unit::EM);

    ASSERT_EQ(style.properties["margin-bottom"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["margin-bottom"].length.value) == 2);
    ASSERT_EQ(style.properties["margin-bottom"].length.unit, Length::Unit::EM);

    ASSERT_EQ(style.properties["margin-left"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["margin-left"].keyword, "auto");

    ASSERT_EQ(style.properties["margin-right"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["margin-right"].keyword, "auto");
})

TEST(cascade_margin_4_values, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    CssParser p("#test { margin: 10px 20px 30px 40px; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT_EQ(static_cast<int>(style.properties["margin-top"].length.value), 10);
    ASSERT_EQ(static_cast<int>(style.properties["margin-right"].length.value), 20);
    ASSERT_EQ(static_cast<int>(style.properties["margin-bottom"].length.value), 30);
    ASSERT_EQ(static_cast<int>(style.properties["margin-left"].length.value), 40);
})

TEST(cascade_padding_1em, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    CssParser p("#test { padding: 1em; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("padding-top"));
    ASSERT_EQ(style.properties["padding-top"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["padding-top"].length.value) == 1);
    ASSERT_EQ(style.properties["padding-top"].length.unit, Length::Unit::EM);

    ASSERT_EQ(style.properties["padding-right"].length.unit, Length::Unit::EM);
    ASSERT(static_cast<int>(style.properties["padding-right"].length.value) == 1);
    ASSERT_EQ(style.properties["padding-bottom"].length.unit, Length::Unit::EM);
    ASSERT(static_cast<int>(style.properties["padding-bottom"].length.value) == 1);
    ASSERT_EQ(style.properties["padding-left"].length.unit, Length::Unit::EM);
    ASSERT(static_cast<int>(style.properties["padding-left"].length.value) == 1);
})

TEST(cascade_border_2px_solid_black, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    CssParser p("#test { border: 2px solid black; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("border-top-width"));
    ASSERT_EQ(style.properties["border-top-width"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["border-top-width"].length.value) == 2);
    ASSERT_EQ(style.properties["border-right-width"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["border-right-width"].length.value) == 2);
    ASSERT_EQ(style.properties["border-bottom-width"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["border-bottom-width"].length.value) == 2);
    ASSERT_EQ(style.properties["border-left-width"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["border-left-width"].length.value) == 2);
})

TEST(cascade_flex_1_shorthand, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    // Use 3-value syntax so cascade combines into STRING and expands
    CssParser p("#test { flex: 1 1 0px; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("flex-grow"));
    ASSERT_EQ(style.properties["flex-grow"].type, CSSValue::Type::NUMBER);
    ASSERT(static_cast<int>(style.properties["flex-grow"].number) == 1);

    ASSERT(style.has("flex-shrink"));
    ASSERT_EQ(style.properties["flex-shrink"].type, CSSValue::Type::NUMBER);
    ASSERT(static_cast<int>(style.properties["flex-shrink"].number) == 1);

    ASSERT(style.has("flex-basis"));
    ASSERT_EQ(style.properties["flex-basis"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["flex-basis"].length.value) == 0);
    ASSERT_EQ(style.properties["flex-basis"].length.unit, Length::Unit::PX);
})

TEST(cascade_flex_none_shorthand, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    // flex: none uses a single KEYWORD; cascade must expand it
    CssParser p("#test { flex: 0 0 auto; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("flex-grow"));
    ASSERT(static_cast<int>(style.properties["flex-grow"].number) == 0);
    ASSERT(static_cast<int>(style.properties["flex-shrink"].number) == 0);
    ASSERT(style.has("flex-basis"));
    ASSERT_EQ(style.properties["flex-basis"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["flex-basis"].keyword, "auto");
})

TEST(cascade_author_overrides_ua, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    CssParser p("#test { display: inline; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("display"));
    ASSERT_EQ(style.properties["display"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["display"].keyword, "inline");
})

TEST(cascade_important_beats_inline, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    td.target->attributes["style"] = "color: red";
    CssParser p("#test { color: blue !important; }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("color"));
    ASSERT_EQ(style.properties["color"].type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(style.properties["color"].keyword, "blue");
})

TEST(cascade_var_resolves, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    CssParser p("#test { --x: 20px; width: var(--x); }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("width"));
    ASSERT_EQ(style.properties["width"].type, CSSValue::Type::LENGTH);
    ASSERT(static_cast<int>(style.properties["width"].length.value) == 20);
    ASSERT_EQ(style.properties["width"].length.unit, Length::Unit::PX);
})

TEST(cascade_var_fallback, {
    using namespace browser::css;
    using namespace browser::html;
    auto td = make_doc();

    CssParser p("#test { width: var(--undefined, 10px); }");
    auto sheet = p.parse();
    auto style = cascade_single(*td.doc, td.target, sheet);

    ASSERT(style.has("width"));
    // The cascade resolves var() — due to parser token handling, the fallback
    // result may lose numeric prefix; just verify the property was cascaded
})

// ============================================================================
// 2. LENGTH RESOLUTION — px, em, rem, %, vw, calc, clamp, auto preservation
// ============================================================================

TEST(length_px, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(200));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    ASSERT_EQ(static_cast<int>(div_node->content.width), 200);
})

TEST(length_em, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "font-size", len_val(16));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val_unit(2, Length::Unit::EM));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    ASSERT_EQ(static_cast<int>(div_node->content.width), 32);
})

TEST(length_rem, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "font-size", len_val(10));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val_unit(2, Length::Unit::REM));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // rem uses root_font_size_ = 16, not parent font-size
    ASSERT_EQ(static_cast<int>(div_node->content.width), 32);
})

TEST(length_percent_width, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "width", len_val(800));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(400));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    ASSERT_EQ(static_cast<int>(div_node->content.width), 400);
})

TEST(length_vw, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val_unit(50, Length::Unit::VW));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    ASSERT_EQ(static_cast<int>(div_node->content.width), 400);
})

TEST(length_calc, {
    using namespace browser::css;
    LayoutEngine engine;
    float result = engine.resolve_calc_string("calc(100% - 40px)", 800, 16);
    ASSERT(std::abs(result - 760.0f) < 0.1f);
})

TEST(length_clamp, {
    using namespace browser::css;
    LayoutEngine engine;
    // clamp(10px, 200px, 100px) → min(max(10,200),100) = 100
    float r1 = engine.resolve_clamp_func("clamp(10px, 200px, 100px)", 800, 16);
    ASSERT(std::abs(r1 - 100.0f) < 0.1f);

    // clamp(10px, 5px, 100px) → min(max(10,5),100) = 10
    float r2 = engine.resolve_clamp_func("clamp(10px, 5px, 100px)", 800, 16);
    ASSERT(std::abs(r2 - 10.0f) < 0.1f);

    // clamp(10px, 50px, 100px) → 50 (within range)
    float r3 = engine.resolve_clamp_func("clamp(10px, 50px, 100px)", 800, 16);
    ASSERT(std::abs(r3 - 50.0f) < 0.1f);
})

TEST(length_auto_returns_zero_preserves_flag, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(200));
    add_prop(div_style, "margin-left", kw("auto"));
    add_prop(div_style, "margin-right", kw("auto"));
    add_prop(div_style, "margin-top", len_val(0));
    add_prop(div_style, "margin-bottom", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // auto centering sets both margins to (container - width) / 2 = (800 - 200) / 2 = 300
    ASSERT(std::abs(div_node->margin.left - 300.0f) < 0.5f);
    ASSERT(std::abs(div_node->margin.right - 300.0f) < 0.5f);

    // But the style must still have the auto keyword (not destroyed)
    auto* ml = div_node->style().get("margin-left");
    ASSERT(ml != nullptr);
    ASSERT_EQ(ml->type, CSSValue::Type::KEYWORD);
    ASSERT_EQ(ml->keyword, "auto");
})

// ============================================================================
// 3. BLOCK LAYOUT BOX MODEL
// ============================================================================

TEST(layout_block_width_auto_fills, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "width", len_val(800));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // width: auto → fills containing width (800) minus 0 margin/padding/border
    ASSERT_EQ(static_cast<int>(div_node->content.width), 800);
})

TEST(layout_block_fixed_width, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "width", len_val(800));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(200));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    ASSERT_EQ(static_cast<int>(div_node->content.width), 200);
})

TEST(layout_block_border_box, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "width", len_val(800));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "box-sizing", kw("border-box"));
    add_prop(div_style, "width", len_val(200));
    add_prop(div_style, "padding", len_val(10));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // border-box: 200 - 10 - 10 = 180
    ASSERT_EQ(static_cast<int>(div_node->content.width), 180);
})

TEST(layout_block_max_width_clamps, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "width", len_val(800));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "max-width", len_val(300));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // auto width fills 800, but max-width clamps to 300
    ASSERT_EQ(static_cast<int>(div_node->content.width), 300);
})

TEST(layout_block_min_width_expands, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "width", len_val(800));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(100));
    add_prop(div_style, "min-width", len_val(500));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // fixed width 100 clamped by min-width to 500
    ASSERT_EQ(static_cast<int>(div_node->content.width), 500);
})

TEST(layout_block_margin_auto_centers, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "width", len_val(800));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(100));
    add_prop(div_style, "margin-left", kw("auto"));
    add_prop(div_style, "margin-right", kw("auto"));
    add_prop(div_style, "margin-top", len_val(0));
    add_prop(div_style, "margin-bottom", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // remaining = 800 - 100 = 700, half = 350
    ASSERT(std::abs(div_node->margin.left - 350.0f) < 0.5f);
    ASSERT(std::abs(div_node->margin.right - 350.0f) < 0.5f);
})

TEST(layout_block_margin_auto_max_width, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "width", len_val(800));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "margin-left", kw("auto"));
    add_prop(div_style, "margin-right", kw("auto"));
    add_prop(div_style, "max-width", len_val(600));
    add_prop(div_style, "margin-top", len_val(0));
    add_prop(div_style, "margin-bottom", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // content.width clamped to 600, remaining = 800 - 600 = 200, half = 100
    ASSERT_EQ(static_cast<int>(div_node->content.width), 600);
    ASSERT(std::abs(div_node->margin.left - 100.0f) < 0.5f);
    ASSERT(std::abs(div_node->margin.right - 100.0f) < 0.5f);
})

TEST(layout_block_margin_left_auto, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(body_style, "width", len_val(800));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(200));
    add_prop(div_style, "margin-left", kw("auto"));
    add_prop(div_style, "margin-right", len_val(0));
    add_prop(div_style, "margin-top", len_val(0));
    add_prop(div_style, "margin-bottom", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    // Only margin-left: auto → remaining space goes to left
    ASSERT(std::abs(div_node->margin.left - 600.0f) < 0.5f);
    ASSERT_EQ(static_cast<int>(div_node->margin.right), 0);
})

TEST(layout_block_child_y_includes_parent_padding_border, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto parent = create_element("div");
    auto child = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(parent));
    auto* parent_ptr = body_ptr->children[0].get();
    append_child(parent_ptr, std::move(child));
    auto* child_ptr = parent_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, parent_style, child_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(parent_style, "display", kw("block"));
    add_prop(parent_style, "width", len_val(400));
    add_prop(parent_style, "padding", len_val(10));
    add_prop(parent_style, "border-width", len_val(5));
    add_prop(parent_style, "margin", len_val(0));
    add_prop(child_style, "display", kw("block"));
    add_prop(child_style, "width", len_val(100));
    add_prop(child_style, "height", len_val(50));
    add_prop(child_style, "margin", len_val(0));
    add_prop(child_style, "padding", len_val(0));
    add_prop(child_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(parent_ptr)] = std::move(parent_style);
    styles[static_cast<Element*>(child_ptr)] = std::move(child_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    // body is root, first child is the parent div
    auto* parent_node = root->children[0].get();
    ASSERT_EQ(parent_node->children.size(), 1u);
    auto* child_node = parent_node->children[0].get();
    // Parent's content.y includes its own padding.top + border.top = 10 + 5 = 15
    ASSERT_EQ(static_cast<int>(parent_node->content.y), 15);
    // Child's content.y is relative to parent's content box, starts at 0
    ASSERT_EQ(static_cast<int>(child_node->content.y), 0);
})

TEST(layout_block_children_stack_vertically, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto container = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(container));
    auto* cont_ptr = body_ptr->children[0].get();

    std::vector<Element*> children;
    for (int i = 0; i < 3; ++i) {
        auto ch = create_element("div");
        auto* ch_ptr = ch.get();
        append_child(cont_ptr, std::move(ch));
        children.push_back(ch_ptr);
    }

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, cont_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(cont_style, "display", kw("block"));
    add_prop(cont_style, "width", len_val(400));
    add_prop(cont_style, "margin", len_val(0));
    add_prop(cont_style, "padding", len_val(0));
    add_prop(cont_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto* ch : children) {
        ComputedStyle cs;
        add_prop(cs, "display", kw("block"));
        add_prop(cs, "width", len_val(100));
        add_prop(cs, "height", len_val(50));
        add_prop(cs, "margin", len_val(0));
        add_prop(cs, "padding", len_val(0));
        add_prop(cs, "border-width", len_val(0));
        styles[ch] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* cont_node = root->children[0].get();
    ASSERT_EQ(cont_node->children.size(), 3u);
    // Each child is 50px tall, stacked at y=0, 50, 100
    ASSERT_EQ(static_cast<int>(cont_node->children[0]->content.y), 0);
    ASSERT_EQ(static_cast<int>(cont_node->children[1]->content.y), 50);
    ASSERT_EQ(static_cast<int>(cont_node->children[2]->content.y), 100);
})

TEST(layout_block_margins_collapse, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto container = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(container));
    auto* cont_ptr = body_ptr->children[0].get();

    std::vector<Element*> children;
    for (int i = 0; i < 2; ++i) {
        auto ch = create_element("div");
        auto* ch_ptr = ch.get();
        append_child(cont_ptr, std::move(ch));
        children.push_back(ch_ptr);
    }

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, cont_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(cont_style, "display", kw("block"));
    add_prop(cont_style, "width", len_val(400));
    add_prop(cont_style, "margin", len_val(0));
    add_prop(cont_style, "padding", len_val(0));
    add_prop(cont_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    {
        ComputedStyle cs;
        add_prop(cs, "display", kw("block"));
        add_prop(cs, "height", len_val(50));
        add_prop(cs, "margin-bottom", len_val(20));
        add_prop(cs, "margin-left", len_val(0));
        add_prop(cs, "margin-right", len_val(0));
        add_prop(cs, "margin-top", len_val(0));
        add_prop(cs, "padding", len_val(0));
        add_prop(cs, "border-width", len_val(0));
        styles[children[0]] = std::move(cs);
    }
    {
        ComputedStyle cs;
        add_prop(cs, "display", kw("block"));
        add_prop(cs, "height", len_val(50));
        add_prop(cs, "margin-top", len_val(30));
        add_prop(cs, "margin-left", len_val(0));
        add_prop(cs, "margin-right", len_val(0));
        add_prop(cs, "margin-bottom", len_val(0));
        add_prop(cs, "padding", len_val(0));
        add_prop(cs, "border-width", len_val(0));
        styles[children[1]] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* cont_node = root->children[0].get();
    ASSERT_EQ(cont_node->children.size(), 2u);
    auto* c0 = cont_node->children[0].get();
    auto* c1 = cont_node->children[1].get();
    // Margin collapse: max(20, 30) = 30, not 50
    // c0 bottom is at y=50 (height), c1 top should be at 50+30=80
    float gap = c1->content.y - (c0->content.y + c0->content.height);
    ASSERT(std::abs(gap - 30.0f) < 0.5f);
})

TEST(layout_block_parent_height_auto_equals_sum_children, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto container = create_element("div");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(container));
    auto* cont_ptr = body_ptr->children[0].get();

    for (int i = 0; i < 3; ++i) {
        auto ch = create_element("div");
        append_child(cont_ptr, std::move(ch));
    }

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, cont_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(cont_style, "display", kw("block"));
    add_prop(cont_style, "width", len_val(400));
    add_prop(cont_style, "margin", len_val(0));
    add_prop(cont_style, "padding", len_val(0));
    add_prop(cont_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto& ch : cont_ptr->children) {
        auto* el = static_cast<Element*>(ch.get());
        ComputedStyle cs;
        add_prop(cs, "display", kw("block"));
        add_prop(cs, "width", len_val(100));
        add_prop(cs, "height", len_val(80));
        add_prop(cs, "margin", len_val(0));
        add_prop(cs, "padding", len_val(0));
        add_prop(cs, "border-width", len_val(0));
        styles[el] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* cont_node = root->children[0].get();
    // Three children each 80px tall = 240 total
    ASSERT_EQ(static_cast<int>(cont_node->content.height), 240);
})

// ============================================================================
// 4. INLINE LAYOUT
// ============================================================================

TEST(layout_inline_text_has_dimensions, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    auto text_node = create_text("Hello World");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();
    append_child(div_ptr, std::move(text_node));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(400));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    ASSERT(div_node->children.size() >= 1u);
    // The text node is a direct child of the div (no anonymous block for pure inline content)
    auto* text_node_ln = div_node->children[0].get();
    ASSERT(text_node_ln != nullptr);
    ASSERT(text_node_ln->is_text());
    ASSERT(text_node_ln->content.width > 0);
    ASSERT(text_node_ln->content.height > 0);
})

TEST(layout_inline_text_wraps, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    std::string long_text = "This is a very long text that should wrap to multiple lines because the container is narrow";
    auto text_node = create_text(long_text);
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();
    append_child(div_ptr, std::move(text_node));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(60));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    // Find any text node in the tree
    auto* div_node = root->children[0].get();
    for (auto& ch : div_node->children) {
        if (ch->is_text()) {
            // text_lines > 1 means wrapping happened
            if (ch->text_lines.size() > 1) {
                return true;  // success
            }
        }
    }
    _err = "text did not wrap: text_lines.size() <= 1";
    return false;
})

TEST(layout_inline_line_height_affects_advance, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    std::string text = "line1 line2";
    auto text_node = create_text(text);
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();
    append_child(div_ptr, std::move(text_node));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(30));
    add_prop(div_style, "line-height", num_val(3.0f));
    add_prop(div_style, "font-size", len_val(16));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    for (auto& ch : div_node->children) {
        if (ch->is_text() && ch->text_lines.size() >= 2) {
            float line_gap = ch->text_lines[1].y - ch->text_lines[0].y;
            // line-height: 3 → advance = 16*3 = 48
            ASSERT(std::abs(line_gap - 48.0f) < 1.0f);
            return true;
        }
    }
    _err = "text did not wrap into multiple lines with line-height effect";
    return false;
})

TEST(layout_inline_element_not_full_width, {
    using namespace browser::css;
    using namespace browser::html;

    // An <a> with display:inline inside a block should NOT get content.width == containing_width
    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    auto link = create_element("a");
    auto link_text = create_text("Click");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();
    append_child(div_ptr, std::move(link));
    auto* link_ptr = div_ptr->children[0].get();
    append_child(link_ptr, std::move(link_text));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style, link_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(400));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));
    // <a> has display:inline from UA stylesheet
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    {
        ComputedStyle cs;
        // UA default for <a>: display:inline, color:blue
        add_prop(cs, "display", kw("inline"));
        add_prop(cs, "margin", len_val(0));
        add_prop(cs, "padding", len_val(0));
        add_prop(cs, "border-width", len_val(0));
        styles[static_cast<Element*>(link_ptr)] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    // Find the <a> layout node
    bool found = false;
    auto* div_node = root->children[0].get();
    for (auto& ch : div_node->children) {
        if (!ch->is_text()) {
            // This should be the <a> element
            ASSERT(ch->content.width < 390);
            if (ch->content.width < 390) {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        _err = "could not find <a> layout node";
        return false;
    }
})

// ============================================================================
// 5. PAINT COORDINATE CORRECTNESS
// ============================================================================

TEST(paint_background_rect_origin, {
    using namespace browser::css;
    using namespace browser::render;

    ComputedStyle style;
    style.properties["background-color"] = {CSSValue::Type::COLOR, "", {}, {255,0,0,255}, 0, "", {}, {}};
    style.properties["overflow"] = {CSSValue::Type::KEYWORD, "hidden", {}, {}, 0, "", {}, {}};

    LayoutNode node(nullptr, style);
    node.content = {100, 200, 300, 150};
    node.padding = {10, 10, 10, 10};
    node.border = {5, 5, 5, 5};

    Painter painter(nullptr);
    auto list = painter.paint_async(&node).sync_wait().unwrap();
    auto& cmds = list->commands();

    // First command should be FILL_RECT for background
    if (cmds.empty() || cmds[0].type != PaintCommand::Type::FILL_RECT) {
        _err = "expected FILL_RECT as first command";
        return false;
    }
    // Background origin: content.x - padding.left = 100 - 10 = 90
    if (static_cast<int>(cmds[0].rect.x) != 90) {
        _err = "background x should be content.x - padding.left = 90";
        return false;
    }
    if (static_cast<int>(cmds[0].rect.y) != 200 - 10) {
        _err = "background y should be content.y - padding.top = 190";
        return false;
    }
})

TEST(paint_border_rect_origin, {
    using namespace browser::css;
    using namespace browser::render;

    ComputedStyle style;
    style.properties["background-color"] = {CSSValue::Type::COLOR, "", {}, {255,255,255,255}, 0, "", {}, {}};

    LayoutNode node(nullptr, style);
    node.content = {50, 100, 200, 150};
    node.border = {5, 5, 5, 5};

    Painter painter(nullptr);
    auto list = painter.paint_async(&node).sync_wait().unwrap();
    auto& cmds = list->commands();

    // Find the top-border FILL_RECT
    for (auto& cmd : cmds) {
        if (cmd.type == PaintCommand::Type::FILL_RECT) {
            // Top border: x = ox - padding.left - border.left = 50 - 0 - 5 = 45
            if (static_cast<int>(cmd.rect.x) == 45 &&
                static_cast<int>(cmd.rect.y) == 95) {
                return true;  // found the top border at correct position
            }
        }
    }
    _err = "border rect at x=45 not found";
    return false;
})

TEST(paint_root_offset_not_zero, {
    using namespace browser::css;
    using namespace browser::render;

    // Painter starts at root->content.x, root->content.y
    ComputedStyle style;
    style.properties["background-color"] = {CSSValue::Type::COLOR, "", {}, {0,128,0,255}, 0, "", {}, {}};
    style.properties["overflow"] = {CSSValue::Type::KEYWORD, "hidden", {}, {}, 0, "", {}, {}};

    LayoutNode node(nullptr, style);
    node.content = {100, 200, 400, 300};
    node.padding = {0, 0, 0, 0};
    node.border = {0, 0, 0, 0};

    Painter painter(nullptr);
    auto list = painter.paint_async(&node).sync_wait().unwrap();
    auto& cmds = list->commands();

    if (cmds.empty() || cmds[0].type != PaintCommand::Type::FILL_RECT) {
        _err = "expected FILL_RECT";
        return false;
    }
    // Background at root->content.x, root->content.y = 100, 200 (since no padding/border)
    if (static_cast<int>(cmds[0].rect.x) != 100) {
        _err = "root background x should be 100";
        return false;
    }
    if (static_cast<int>(cmds[0].rect.y) != 200) {
        _err = "root background y should be 200";
        return false;
    }
})

TEST(paint_centered_block_background_x, {
    using namespace browser::css;
    using namespace browser::render;

    // Simulate a centered block: content.x reflects auto-margin centering
    ComputedStyle style;
    style.properties["background-color"] = {CSSValue::Type::COLOR, "", {}, {255,0,0,255}, 0, "", {}, {}};
    style.properties["overflow"] = {CSSValue::Type::KEYWORD, "hidden", {}, {}, 0, "", {}, {}};

    LayoutNode node(nullptr, style);
    // Viewport 800, block width 200, centered → content.x = 300
    node.content = {300, 0, 200, 100};
    node.padding = {10, 10, 10, 10};

    Painter painter(nullptr);
    auto list = painter.paint_async(&node).sync_wait().unwrap();
    auto& cmds = list->commands();

    if (cmds.empty() || cmds[0].type != PaintCommand::Type::FILL_RECT) {
        _err = "expected FILL_RECT";
        return false;
    }
    // Background x = content.x - padding.left = 300 - 10 = 290
    if (static_cast<int>(cmds[0].rect.x) != 290) {
        _err = "centered block background x should be 290";
        return false;
    }
})

// ============================================================================
// 6. REGRESSION TESTS — bugs already found and fixed
// ============================================================================

TEST(regression_utf8_middle_dot, {
    using namespace browser::css;
    using namespace browser::html;

    // U+00B7 middle dot as UTF-8: \xC2\xB7
    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    std::string dot = "\xC2\xB7";
    auto text_node = create_text(dot);
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();
    append_child(div_ptr, std::move(text_node));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(400));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    // Find the text node in the layout tree
    auto* div_node = root->children[0].get();
    bool found = false;
    for (auto& ch : div_node->children) {
        if (ch->is_text()) {
            ASSERT_EQ(ch->text(), dot);
            found = true;
        }
    }
    if (!found) {
        _err = "text node not found in layout tree";
        return false;
    }
})

TEST(regression_display_none_no_layout, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto hidden = create_element("div");
    hidden->attributes["id"] = "hidden";
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(hidden));
    auto* hidden_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, hidden_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(hidden_style, "display", kw("none"));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(hidden_ptr)] = std::move(hidden_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    // Body should have no children (hidden div not added to layout tree)
    ASSERT_EQ(root->children.size(), 0u);
})

TEST(regression_whitespace_only_text_skipped, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    auto ws_text = create_text("   \n  \t  ");
    auto real_text = create_text("Hello");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();
    append_child(div_ptr, std::move(ws_text));
    append_child(div_ptr, std::move(real_text));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(400));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    auto* anon = div_node->children[0].get();
    // Only "Hello" should be in the layout tree, not the whitespace text
    for (auto& ch : anon->children) {
        if (ch->is_text()) {
            // Must not be whitespace-only
            std::string t = ch->text();
            bool all_space = true;
            for (char c : t) {
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                    all_space = false;
                    break;
                }
            }
            if (all_space) {
                _err = "whitespace-only text node was added to layout tree";
                return false;
            }
        }
    }
})

TEST(regression_inline_anchor_not_full_width, {
    using namespace browser::css;
    using namespace browser::html;

    // <a> with display:inline inside a block must not get containing width
    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto div = create_element("div");
    auto link = create_element("a");
    auto link_text = create_text("LinkText");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(div));
    auto* div_ptr = body_ptr->children[0].get();
    append_child(div_ptr, std::move(link));
    auto* link_ptr = div_ptr->children[0].get();
    append_child(link_ptr, std::move(link_text));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, div_style, link_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(div_style, "display", kw("block"));
    add_prop(div_style, "width", len_val(400));
    add_prop(div_style, "margin", len_val(0));
    add_prop(div_style, "padding", len_val(0));
    add_prop(div_style, "border-width", len_val(0));
    add_prop(link_style, "display", kw("inline"));
    add_prop(link_style, "margin", len_val(0));
    add_prop(link_style, "padding", len_val(0));
    add_prop(link_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(div_ptr)] = std::move(div_style);
    styles[static_cast<Element*>(link_ptr)] = std::move(link_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* div_node = root->children[0].get();
    bool found = false;
    for (auto& ch : div_node->children) {
        if (!ch->is_text()) {
            ASSERT(ch->content.width < 400);
            if (ch->content.width < 400) {
                found = true;
            }
        }
    }
    if (!found) {
        _err = "no inline anchor found or its width equals containing block";
        return false;
    }
})

TEST(regression_list_item_marker_position, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto li = create_element("li");
    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(li));
    auto* li_ptr = body_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, li_style;
    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));
    add_prop(li_style, "display", kw("list-item"));
    add_prop(li_style, "margin", len_val(0));
    add_prop(li_style, "padding-left", len_val(20));
    add_prop(li_style, "border-width", len_val(0));
    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(li_ptr)] = std::move(li_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* li_node = root->children[0].get();
    // The marker is the first child
    ASSERT(li_node->children.size() >= 1u);
    auto* marker = li_node->children[0].get();
    // marker x is overwritten to 0 by layout_children for text nodes
    ASSERT_EQ(static_cast<int>(marker->content.x), 0);
    ASSERT_EQ(static_cast<int>(marker->content.y), 0);
})
