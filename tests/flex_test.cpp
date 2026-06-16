#include "test_framework.hpp"
#include "../css/layout.hpp"
#include "../html/dom.hpp"
#include "../html/parser.hpp"
#include <unordered_map>
#include <memory>
#include "../async/task.hpp"

static void add_style_prop(browser::css::ComputedStyle& style,
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

static browser::css::CSSValue num_val(browser::f32 value) {
    browser::css::CSSValue v;
    v.type = browser::css::CSSValue::Type::NUMBER;
    v.number = value;
    return v;
}

TEST(flex_base_size, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto container = create_element("div");
    auto child = create_element("div");

    container->attributes["id"] = "container";
    child->attributes["id"] = "child";

    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(container));
    auto* cont_ptr = body_ptr->children[0].get();
    append_child(cont_ptr, std::move(child));
    auto* child_ptr = cont_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, cont_style, child_style;

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));

    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "width", len_val(600));
    add_style_prop(cont_style, "height", len_val(100));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));

    add_style_prop(child_style, "flex-basis", len_val(100));
    add_style_prop(child_style, "height", len_val(40));
    add_style_prop(child_style, "margin", len_val(0));
    add_style_prop(child_style, "padding", len_val(0));
    add_style_prop(child_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);
    styles[static_cast<Element*>(child_ptr)] = std::move(child_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* container_node = root.get();
    // body is root, first child should be the container
    ASSERT_EQ(container_node->children.size(), 1u);
    auto* flex_node = container_node->children[0].get();
    ASSERT_EQ(flex_node->children.size(), 1u);
    auto* child_node = flex_node->children[0].get();
    ASSERT_EQ(static_cast<int>(child_node->content.width), 100);
})

TEST(flex_base_auto, {
    using namespace browser::css;
    using namespace browser::html;

    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto container = create_element("div");
    auto child = create_element("div");

    container->attributes["id"] = "container";
    child->attributes["id"] = "child";

    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(container));
    auto* cont_ptr = body_ptr->children[0].get();
    append_child(cont_ptr, std::move(child));
    auto* child_ptr = cont_ptr->children[0].get();

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, cont_style, child_style;

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));
    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "width", len_val(600));
    add_style_prop(cont_style, "height", len_val(100));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));
    add_style_prop(child_style, "width", len_val(200));
    add_style_prop(child_style, "height", len_val(40));
    add_style_prop(child_style, "margin", len_val(0));
    add_style_prop(child_style, "padding", len_val(0));
    add_style_prop(child_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);
    styles[static_cast<Element*>(child_ptr)] = std::move(child_style);

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* flex_node = root->children[0].get();
    auto* child_node = flex_node->children[0].get();
    ASSERT_EQ(static_cast<int>(child_node->content.width), 200);
})

TEST(flex_grow_equal, {
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

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));
    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "width", len_val(600));
    add_style_prop(cont_style, "height", len_val(100));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto* ch : children) {
        ComputedStyle cs;
        add_style_prop(cs, "flex-grow", num_val(1));
        add_style_prop(cs, "height", len_val(40));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[ch] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* flex_node = root->children[0].get();
    ASSERT_EQ(flex_node->children.size(), 3u);
    for (auto& ch : flex_node->children) {
        ASSERT_EQ(static_cast<int>(ch->content.width), 200);
    }
})

TEST(flex_grow_unequal, {
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

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));
    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "width", len_val(300));
    add_style_prop(cont_style, "height", len_val(100));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    // child0 flex-grow:1, child1 flex-grow:2 -> widths 100 and 200
    {
        ComputedStyle cs;
        add_style_prop(cs, "flex-grow", num_val(1));
        add_style_prop(cs, "height", len_val(40));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[children[0]] = std::move(cs);
    }
    {
        ComputedStyle cs;
        add_style_prop(cs, "flex-grow", num_val(2));
        add_style_prop(cs, "height", len_val(40));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[children[1]] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* flex_node = root->children[0].get();
    ASSERT_EQ(flex_node->children.size(), 2u);
    ASSERT_EQ(static_cast<int>(flex_node->children[0]->content.width), 100);
    ASSERT_EQ(static_cast<int>(flex_node->children[1]->content.width), 200);
})

TEST(flex_shrink, {
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

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));
    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "width", len_val(200));
    add_style_prop(cont_style, "height", len_val(100));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto* ch : children) {
        ComputedStyle cs;
        add_style_prop(cs, "flex-shrink", num_val(1));
        add_style_prop(cs, "width", len_val(150));
        add_style_prop(cs, "height", len_val(40));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[ch] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* flex_node = root->children[0].get();
    ASSERT_EQ(flex_node->children.size(), 2u);
    // total hypothetical = 300, container = 200, free = -100
    // each shrinks by 50, target = 100
    ASSERT_EQ(static_cast<int>(flex_node->children[0]->content.width), 100);
    ASSERT_EQ(static_cast<int>(flex_node->children[1]->content.width), 100);
})

TEST(flex_shrink_zero, {
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

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));
    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "width", len_val(200));
    add_style_prop(cont_style, "height", len_val(100));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    // First child has flex-shrink: 1, second has flex-shrink: 0
    {
        ComputedStyle cs;
        add_style_prop(cs, "flex-shrink", num_val(1));
        add_style_prop(cs, "width", len_val(150));
        add_style_prop(cs, "height", len_val(40));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[children[0]] = std::move(cs);
    }
    {
        ComputedStyle cs;
        add_style_prop(cs, "flex-shrink", num_val(0));
        add_style_prop(cs, "width", len_val(150));
        add_style_prop(cs, "height", len_val(40));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[children[1]] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* flex_node = root->children[0].get();
    ASSERT_EQ(flex_node->children.size(), 2u);
    // total hypothetical = 300, container = 200, free = -100
    // total_scaled_shrink = 1*150 + 0*150 = 150
    // child0: 150 + (-100) * (150/150) = 50
    // child1: flex-shrink:0, so keeps 150 (exceeds container, but that's expected)
    ASSERT_EQ(static_cast<int>(flex_node->children[0]->content.width), 50);
    ASSERT_EQ(static_cast<int>(flex_node->children[1]->content.width), 150);
})

TEST(flex_wrap_basic, {
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
    for (int i = 0; i < 5; ++i) {
        auto ch = create_element("div");
        auto* ch_ptr = ch.get();
        append_child(cont_ptr, std::move(ch));
        children.push_back(ch_ptr);
    }

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, cont_style;

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));
    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "flex-wrap", kw("wrap"));
    add_style_prop(cont_style, "width", len_val(400));
    add_style_prop(cont_style, "height", len_val(300));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto* ch : children) {
        ComputedStyle cs;
        add_style_prop(cs, "width", len_val(200));
        add_style_prop(cs, "height", len_val(40));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[ch] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* flex_node = root->children[0].get();
    // 5 children at 200px each in 400px container with wrap
    // line 1: items 0,1 (400px), line 2: items 2,3 (400px), line 3: item 4
    ASSERT_EQ(flex_node->children.size(), 5u);
    // Items 0 and 1 should be on the same y
    ASSERT_EQ(static_cast<int>(flex_node->children[0]->content.y),
              static_cast<int>(flex_node->children[1]->content.y));
    // Items 2 should be on a new line (y increased)
    ASSERT(flex_node->children[2]->content.y > flex_node->children[0]->content.y);
})

TEST(flex_row_reverse, {
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

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));
    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "flex-direction", kw("row-reverse"));
    add_style_prop(cont_style, "width", len_val(600));
    add_style_prop(cont_style, "height", len_val(100));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto* ch : children) {
        ComputedStyle cs;
        add_style_prop(cs, "flex-grow", num_val(1));
        add_style_prop(cs, "height", len_val(40));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[ch] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* flex_node = root->children[0].get();
    ASSERT_EQ(flex_node->children.size(), 3u);
    // row-reverse reverses items within the line; first DOM child goes last
    ASSERT(flex_node->children[0]->content.x > flex_node->children[2]->content.x);
})

TEST(flex_column, {
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

    add_style_prop(html_style, "display", kw("block"));
    add_style_prop(body_style, "display", kw("block"));
    add_style_prop(body_style, "margin", len_val(0));
    add_style_prop(body_style, "padding", len_val(0));
    add_style_prop(body_style, "border-width", len_val(0));
    add_style_prop(cont_style, "display", kw("flex"));
    add_style_prop(cont_style, "flex-direction", kw("column"));
    add_style_prop(cont_style, "width", len_val(200));
    add_style_prop(cont_style, "height", len_val(300));
    add_style_prop(cont_style, "margin", len_val(0));
    add_style_prop(cont_style, "padding", len_val(0));
    add_style_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto* ch : children) {
        ComputedStyle cs;
        add_style_prop(cs, "flex-grow", num_val(1));
        add_style_prop(cs, "height", len_val(0));
        add_style_prop(cs, "width", len_val(100));
        add_style_prop(cs, "margin", len_val(0));
        add_style_prop(cs, "padding", len_val(0));
        add_style_prop(cs, "border-width", len_val(0));
        styles[ch] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = std::move(engine.layout_async(doc.get(), styles, 800, 600).sync_wait().unwrap());
    ASSERT(root != nullptr);

    auto* flex_node = root->children[0].get();
    ASSERT_EQ(flex_node->children.size(), 3u);
    // Column layout: items stack vertically, each gets 100 height
    ASSERT_EQ(static_cast<int>(flex_node->children[0]->content.height), 100);
    ASSERT_EQ(static_cast<int>(flex_node->children[1]->content.height), 100);
    ASSERT_EQ(static_cast<int>(flex_node->children[2]->content.height), 100);
    // Different y positions
    ASSERT(flex_node->children[1]->content.y > flex_node->children[0]->content.y);
    ASSERT(flex_node->children[2]->content.y > flex_node->children[1]->content.y);
})
