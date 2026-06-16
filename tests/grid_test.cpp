#include "test_framework.hpp"
#include "../css/grid.hpp"
#include "../css/layout.hpp"
#include "../html/dom.hpp"
#include "../html/parser.hpp"
#include <unordered_map>
#include <memory>

using namespace browser;
using namespace browser::css;
using namespace browser::html;

static void add_prop(ComputedStyle& style, const std::string& prop, CSSValue value) {
    style.properties[prop] = std::move(value);
}

static CSSValue kw(const std::string& k) {
    CSSValue v;
    v.type = CSSValue::Type::KEYWORD;
    v.keyword = k;
    return v;
}

static CSSValue len_val(f32 value) {
    CSSValue v;
    v.type = CSSValue::Type::LENGTH;
    v.length = {value, Length::Unit::PX};
    return v;
}

static CSSValue str_val(const std::string& s) {
    CSSValue v;
    v.type = CSSValue::Type::STRING;
    v.string_value = s;
    return v;
}

// ── Step 14.2: Track parsing tests ──────────────────────────────────────

TEST(grid_parse_tracks, {
    using namespace browser::css;
    auto tracks = parse_track_list("100px 1fr auto", 800, 16);
    ASSERT_EQ(tracks.size(), 3u);
    ASSERT_EQ(tracks[0].type, GridTrackType::FIXED);
    ASSERT_EQ(tracks[1].type, GridTrackType::FLEX);
    ASSERT_EQ(tracks[2].type, GridTrackType::AUTO);
})

TEST(grid_parse_minmax, {
    using namespace browser::css;
    auto tracks = parse_track_list("minmax(100px, 1fr)", 800, 16);
    ASSERT_EQ(tracks.size(), 1u);
    ASSERT_EQ(tracks[0].type, GridTrackType::MINMAX);
})

TEST(grid_parse_placement, {
    using namespace browser::css;
    auto p = parse_grid_line("2 / span 3");
    ASSERT_EQ(p.line_start, 2);
    ASSERT_EQ(p.span, 3u);
})

TEST(grid_parse_placement_auto, {
    using namespace browser::css;
    auto p = parse_grid_line("auto");
    ASSERT_EQ(p.line_start, 0);
    ASSERT_EQ(p.span, 1u);
})

TEST(grid_parse_placement_span_only, {
    using namespace browser::css;
    auto p = parse_grid_line("span 2");
    ASSERT_EQ(p.line_start, 0);
    ASSERT_EQ(p.span, 2u);
})

// ── Step 14.3: Track sizing tests ────────────────────────────────────────

TEST(grid_fixed_tracks, {
    LayoutEngine engine;

    std::vector<GridTrackDef> tracks;
    tracks.push_back(GridTrackDef{GridTrackType::FIXED, Length{100, Length::Unit::PX}, Length{100, Length::Unit::PX}});
    tracks.push_back(GridTrackDef{GridTrackType::FIXED, Length{200, Length::Unit::PX}, Length{200, Length::Unit::PX}});
    tracks.push_back(GridTrackDef{GridTrackType::FIXED, Length{300, Length::Unit::PX}, Length{300, Length::Unit::PX}});

    engine.resolve_grid_tracks(tracks, 800, 16);
    ASSERT_EQ(static_cast<int>(tracks[0].resolved_size), 100);
    ASSERT_EQ(static_cast<int>(tracks[1].resolved_size), 200);
    ASSERT_EQ(static_cast<int>(tracks[2].resolved_size), 300);
})

TEST(grid_fr_tracks, {
    LayoutEngine engine;

    std::vector<GridTrackDef> tracks;
    tracks.push_back(GridTrackDef{GridTrackType::FLEX, Length{1, Length::Unit::NONE}, Length{1, Length::Unit::NONE}});
    tracks.push_back(GridTrackDef{GridTrackType::FLEX, Length{2, Length::Unit::NONE}, Length{2, Length::Unit::NONE}});

    engine.resolve_grid_tracks(tracks, 600, 16);
    ASSERT_EQ(static_cast<int>(tracks[0].resolved_size), 200);
    ASSERT_EQ(static_cast<int>(tracks[1].resolved_size), 400);
})

TEST(grid_fr_with_gap, {
    // 3 columns: 1fr 1fr 1fr, gap 10px, container 320px
    // total_gap = 10*2 = 20, remaining = 320 - 0 - 20 = 300, each = 100
    LayoutEngine engine;

    std::vector<GridTrackDef> tracks;
    tracks.push_back(GridTrackDef{GridTrackType::FLEX, Length{1, Length::Unit::NONE}, Length{1, Length::Unit::NONE}});
    tracks.push_back(GridTrackDef{GridTrackType::FLEX, Length{1, Length::Unit::NONE}, Length{1, Length::Unit::NONE}});
    tracks.push_back(GridTrackDef{GridTrackType::FLEX, Length{1, Length::Unit::NONE}, Length{1, Length::Unit::NONE}});

    engine.resolve_grid_tracks(tracks, 320, 16, 10);
    ASSERT_EQ(static_cast<int>(tracks[0].resolved_size), 100);
    ASSERT_EQ(static_cast<int>(tracks[1].resolved_size), 100);
    ASSERT_EQ(static_cast<int>(tracks[2].resolved_size), 100);
})

// ── Grid element detection ──────────────────────────────────────────────

TEST(grid_detection, {
    ComputedStyle style;
    add_prop(style, "display", kw("grid"));
    ASSERT(LayoutEngine::is_grid_element(style));

    ComputedStyle style2;
    add_prop(style2, "display", kw("inline-grid"));
    ASSERT(LayoutEngine::is_grid_element(style2));

    ComputedStyle style3;
    add_prop(style3, "display", kw("flex"));
    ASSERT(!LayoutEngine::is_grid_element(style3));
})

// ── Grid auto-placement ─────────────────────────────────────────────────

TEST(grid_auto_placement_basic, {
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

    add_prop(cont_style, "display", kw("grid"));
    add_prop(cont_style, "grid-template-columns", str_val("100px 100px 100px"));
    add_prop(cont_style, "width", len_val(300));
    add_prop(cont_style, "margin", len_val(0));
    add_prop(cont_style, "padding", len_val(0));
    add_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto* ch : children) {
        ComputedStyle cs;
        add_prop(cs, "margin", len_val(0));
        add_prop(cs, "padding", len_val(0));
        add_prop(cs, "border-width", len_val(0));
        styles[ch] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = engine.layout(doc.get(), styles, 800, 600);
    ASSERT(root != nullptr);

    auto* grid_node = root->children[0].get();
    ASSERT_EQ(grid_node->children.size(), 3u);

    // In 3-column grid, all 3 items should be on the same row
    ASSERT_EQ(static_cast<int>(grid_node->children[0]->content.y),
              static_cast<int>(grid_node->children[1]->content.y));
    ASSERT_EQ(static_cast<int>(grid_node->children[0]->content.y),
              static_cast<int>(grid_node->children[2]->content.y));
    // Different x positions
    ASSERT(grid_node->children[1]->content.x > grid_node->children[0]->content.x);
    ASSERT(grid_node->children[2]->content.x > grid_node->children[1]->content.x);
})

// ── Grid explicit placement ─────────────────────────────────────────────

TEST(grid_explicit_placement, {
    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto container = create_element("div");
    auto child = create_element("div");

    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(container));
    auto* cont_ptr = body_ptr->children[0].get();
    append_child(cont_ptr, std::move(child));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, cont_style, child_style;

    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));

    add_prop(cont_style, "display", kw("grid"));
    add_prop(cont_style, "grid-template-columns", str_val("100px 100px 100px"));
    add_prop(cont_style, "grid-template-rows", str_val("100px 100px"));
    add_prop(cont_style, "width", len_val(300));
    add_prop(cont_style, "margin", len_val(0));
    add_prop(cont_style, "padding", len_val(0));
    add_prop(cont_style, "border-width", len_val(0));

    add_prop(child_style, "grid-column", str_val("2 / span 2"));
    add_prop(child_style, "grid-row", str_val("1"));
    add_prop(child_style, "margin", len_val(0));
    add_prop(child_style, "padding", len_val(0));
    add_prop(child_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    auto* child_ptr_dom = cont_ptr->children[0].get();
    styles[static_cast<Element*>(child_ptr_dom)] = std::move(child_style);

    LayoutEngine engine;
    auto root = engine.layout(doc.get(), styles, 800, 600);
    ASSERT(root != nullptr);

    auto* grid_node = root->children[0].get();
    ASSERT_EQ(grid_node->children.size(), 1u);

    auto* item = grid_node->children[0].get();
    // Item starts at column 2 (0-indexed: 1), so x = 100
    ASSERT_EQ(static_cast<int>(item->content.x), 100);
    // Spans 2 columns, so width = 200
    ASSERT_EQ(static_cast<int>(item->content.width), 200);
})

// ── Grid implicit tracks ────────────────────────────────────────────────

TEST(grid_implicit, {
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

    // 5 items in 2-column grid
    for (int i = 0; i < 5; ++i) {
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

    add_prop(cont_style, "display", kw("grid"));
    add_prop(cont_style, "grid-template-columns", str_val("100px 100px"));
    add_prop(cont_style, "grid-template-rows", str_val("50px 50px 50px"));
    add_prop(cont_style, "width", len_val(200));
    add_prop(cont_style, "margin", len_val(0));
    add_prop(cont_style, "padding", len_val(0));
    add_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto& child : cont_ptr->children) {
        ComputedStyle cs;
        add_prop(cs, "margin", len_val(0));
        add_prop(cs, "padding", len_val(0));
        add_prop(cs, "border-width", len_val(0));
        styles[static_cast<Element*>(child.get())] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = engine.layout(doc.get(), styles, 800, 600);
    ASSERT(root != nullptr);

    auto* grid_node = root->children[0].get();
    // 5 items in 2-column grid → rows fill in row-major order
    ASSERT_EQ(grid_node->children.size(), 5u);

    // Items 0 and 1 on row 0 (same y)
    ASSERT_EQ(static_cast<int>(grid_node->children[0]->content.y),
              static_cast<int>(grid_node->children[1]->content.y));
    // Item 2 starts on row 1 — y should be 50 (height of row 0)
    ASSERT_EQ(static_cast<int>(grid_node->children[2]->content.y), 50);
    // Item 4 starts on row 2 — y should be 100 (50+50)
    ASSERT_EQ(static_cast<int>(grid_node->children[4]->content.y), 100);
})

// ── Grid align-self ─────────────────────────────────────────────────────

TEST(grid_align_self, {
    auto doc = create_document();
    auto html_el = create_element("html");
    auto body = create_element("body");
    auto container = create_element("div");
    auto child = create_element("div");

    append_child(doc.get(), std::move(html_el));
    auto* html_ptr = doc->children[0].get();
    append_child(html_ptr, std::move(body));
    auto* body_ptr = html_ptr->children[0].get();
    append_child(body_ptr, std::move(container));
    auto* cont_ptr = body_ptr->children[0].get();
    append_child(cont_ptr, std::move(child));

    std::unordered_map<const Element*, ComputedStyle> styles;
    ComputedStyle html_style, body_style, cont_style, child_style;

    add_prop(html_style, "display", kw("block"));
    add_prop(body_style, "display", kw("block"));
    add_prop(body_style, "margin", len_val(0));
    add_prop(body_style, "padding", len_val(0));
    add_prop(body_style, "border-width", len_val(0));

    add_prop(cont_style, "display", kw("grid"));
    add_prop(cont_style, "grid-template-columns", str_val("200px"));
    add_prop(cont_style, "grid-template-rows", str_val("200px"));
    add_prop(cont_style, "width", len_val(200));
    add_prop(cont_style, "margin", len_val(0));
    add_prop(cont_style, "padding", len_val(0));
    add_prop(cont_style, "border-width", len_val(0));

    add_prop(child_style, "align-self", kw("center"));
    add_prop(child_style, "margin", len_val(0));
    add_prop(child_style, "padding", len_val(0));
    add_prop(child_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    auto* child_ptr_dom = cont_ptr->children[0].get();
    styles[static_cast<Element*>(child_ptr_dom)] = std::move(child_style);

    LayoutEngine engine;
    auto root = engine.layout(doc.get(), styles, 800, 600);
    ASSERT(root != nullptr);

    auto* grid_node = root->children[0].get();
    auto* item = grid_node->children[0].get();
    // align-self:center → content.y is centered in 200px cell (y=100 for empty item)
    ASSERT_EQ(static_cast<int>(item->content.y), 100);
})

// ── Grid sizing test ────────────────────────────────────────────────────

TEST(grid_sizing, {
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

    for (int i = 0; i < 2; ++i) {
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

    add_prop(cont_style, "display", kw("grid"));
    add_prop(cont_style, "grid-template-columns", str_val("100px 100px"));
    add_prop(cont_style, "width", len_val(200));
    add_prop(cont_style, "margin", len_val(0));
    add_prop(cont_style, "padding", len_val(0));
    add_prop(cont_style, "border-width", len_val(0));

    styles[static_cast<Element*>(html_ptr)] = std::move(html_style);
    styles[static_cast<Element*>(body_ptr)] = std::move(body_style);
    styles[static_cast<Element*>(cont_ptr)] = std::move(cont_style);

    for (auto& child : cont_ptr->children) {
        ComputedStyle cs;
        add_prop(cs, "margin", len_val(0));
        add_prop(cs, "padding", len_val(0));
        add_prop(cs, "border-width", len_val(0));
        styles[static_cast<Element*>(child.get())] = std::move(cs);
    }

    LayoutEngine engine;
    auto root = engine.layout(doc.get(), styles, 800, 600);
    ASSERT(root != nullptr);

    auto* grid_node = root->children[0].get();
    ASSERT_EQ(grid_node->children.size(), 2u);
    // Both items should be 100px wide
    ASSERT_EQ(static_cast<int>(grid_node->children[0]->content.width), 100);
    ASSERT_EQ(static_cast<int>(grid_node->children[1]->content.width), 100);
})
