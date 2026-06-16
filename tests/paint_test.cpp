#include "test_framework.hpp"
#include "../render/paint.hpp"
#include "../render/painter.hpp"
#include "../render/text_renderer.hpp"
#include "../css/layout.hpp"
#include "../async/task.hpp"

// ── Helper: create a CSSValue with a COLOR type ─────────────────────────

static browser::css::CSSValue make_color_val(browser::u8 r, browser::u8 g, browser::u8 b, browser::u8 a = 255) {
    browser::css::CSSValue v;
    v.type = browser::css::CSSValue::Type::COLOR;
    v.color = {r, g, b, a};
    return v;
}

static browser::css::CSSValue make_length_val(browser::f32 px) {
    browser::css::CSSValue v;
    v.type = browser::css::CSSValue::Type::LENGTH;
    v.length = {px, browser::css::Length::Unit::PX};
    return v;
}

// ── Test 1: FILL_RECT emitted for background-color: red ────────────────

static browser::css::CSSValue overflow_hidden_val() {
    browser::css::CSSValue v;
    v.type = browser::css::CSSValue::Type::KEYWORD; v.keyword = "hidden";
    return v;
}

TEST(paint_fill_rect_for_background, {
    browser::css::ComputedStyle style;
    style.properties["background-color"] = make_color_val(255, 0, 0);
    style.properties["overflow"] = overflow_hidden_val();

    browser::css::LayoutNode node(nullptr, style);
    node.content = {0, 0, 200, 100};
    node.border = {0, 0, 0, 0};

    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(&node).sync_wait().unwrap();

    auto& cmds = _list->commands();
    // Background FILL_RECT + PUSH_CLIP + POP_CLIP = 3
    if (cmds.size() != 3) { _err = "expected 3 commands got " + std::to_string(cmds.size()); return false; }
    if (cmds[0].type != browser::render::PaintCommand::Type::FILL_RECT) { _err = "expected FILL_RECT"; return false; }
    if (cmds[0].rect.x != 0) { _err = "x mismatch"; return false; }
    if (cmds[0].rect.y != 0) { _err = "y mismatch"; return false; }
    if (cmds[0].rect.width != 200) { _err = "width mismatch"; return false; }
    if (cmds[0].rect.height != 100) { _err = "height mismatch"; return false; }

    // Check the color conversion: red (255,0,0) → (1,0,0)
    if (cmds[0].color.r != 1.0f) { _err = "color.r mismatch"; return false; }
    if (cmds[0].color.g != 0.0f) { _err = "color.g mismatch"; return false; }
    if (cmds[0].color.b != 0.0f) { _err = "color.b mismatch"; return false; }
})

// ── Test 2: DRAW_TEXT emitted for a text node ──────────────────────────

TEST(paint_draw_text_for_text_node, {
    browser::css::ComputedStyle style;
    style.properties["color"] = make_color_val(0, 0, 0);
    style.properties["font-size"] = make_length_val(16);
    style.properties["overflow"] = overflow_hidden_val();

    browser::css::LayoutNode node("Hello", style);
    node.content = {0, 0, 50, 20};

    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(&node).sync_wait().unwrap();

    auto& cmds = _list->commands();
    // PUSH_CLIP + DRAW_TEXT + POP_CLIP = 3
    if (cmds.size() != 3) { _err = "expected 3 commands got " + std::to_string(cmds.size()); return false; }
    if (cmds[1].type != browser::render::PaintCommand::Type::DRAW_TEXT) { _err = "expected DRAW_TEXT"; return false; }
    if (cmds[1].text != "Hello") { _err = "text mismatch"; return false; }
    if (cmds[1].font_size != 16.0f) { _err = "font_size mismatch"; return false; }
    if (cmds[1].rect.x != 0) { _err = "text x mismatch"; return false; }
    if (cmds[1].rect.y != 0) { _err = "text y mismatch"; return false; }
})

// ── Test 3: PUSH_CLIP/POP_CLIP pairs balanced for non-leaf node ────────

TEST(paint_clip_pairs_balanced, {
    browser::css::ComputedStyle parent_style;
    parent_style.properties["overflow"] = {browser::css::CSSValue::Type::KEYWORD, "hidden", {}, {}, 0, "", {}, {}};
    browser::css::ComputedStyle child_style;
    child_style.properties["overflow"] = {browser::css::CSSValue::Type::KEYWORD, "hidden", {}, {}, 0, "", {}, {}};

    auto parent = std::make_unique<browser::css::LayoutNode>(nullptr, parent_style);
    parent->content = {0, 0, 400, 300};
    parent->padding = {10, 10, 10, 10};

    auto child = std::make_unique<browser::css::LayoutNode>(nullptr, child_style);
    child->content = {10, 10, 200, 100};
    child->parent = parent.get();
    parent->children.push_back(std::move(child));

    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(parent.get()).sync_wait().unwrap();

    auto& cmds = _list->commands();
    int depth = 0;
    unsigned push_count = 0, pop_count = 0;
    for (auto& cmd : cmds) {
        if (cmd.type == browser::render::PaintCommand::Type::PUSH_CLIP) {
            depth++;
            push_count++;
        }
        if (cmd.type == browser::render::PaintCommand::Type::POP_CLIP) {
            depth--;
            pop_count++;
        }
        if (depth < 0) { _err = "negative clip depth"; return false; }
    }
    if (depth != 0) { _err = "unbalanced clips"; return false; }
    if (push_count != 2) { _err = "expected 2 push clips"; return false; }
    if (pop_count != 2) { _err = "expected 2 pop clips"; return false; }
})

// ── Test 4: null root produces no paint commands ───────────────────────

TEST(paint_null_root_produces_no_commands, {
    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(nullptr).sync_wait().unwrap();
    if (_list->commands().size() != 0) { _err = "expected no commands"; return false; }
})

TEST(paint_tree_with_no_visible_children_produces_clips, {
    browser::css::ComputedStyle style;
    style.properties["overflow"] = {browser::css::CSSValue::Type::KEYWORD, "hidden", {}, {}, 0, "", {}, {}};
    browser::css::LayoutNode node(nullptr, style);
    node.content = {0, 0, 100, 50};

    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(&node).sync_wait().unwrap();

    auto& cmds = _list->commands();
    // With overflow:hidden, we get PUSH_CLIP + POP_CLIP = 2
    if (cmds.size() < 2) { _err = "expected at least 2 commands"; return false; }
    if (cmds[0].type != browser::render::PaintCommand::Type::PUSH_CLIP) { _err = "expected PUSH_CLIP"; return false; }
    if (cmds.back().type != browser::render::PaintCommand::Type::POP_CLIP) { _err = "expected POP_CLIP"; return false; }
})

// ── Test 5: Border widths produce correct number of FILL_RECT commands ─

TEST(paint_border_emits_four_fill_rects, {
    browser::css::ComputedStyle style;
    style.properties["background-color"] = make_color_val(255, 255, 255);

    browser::css::LayoutNode node(nullptr, style);
    node.content = {0, 0, 100, 100};
    node.border = {5, 5, 5, 5};

    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(&node).sync_wait().unwrap();

    unsigned fill_rect_count = 0;
    for (auto& cmd : _list->commands()) {
        if (cmd.type == browser::render::PaintCommand::Type::FILL_RECT) {
            fill_rect_count++;
        }
    }
    // 1 background + 4 borders = 5
    if (fill_rect_count != 5) { _err = "expected 5 FILL_RECT commands"; return false; }
})

TEST(paint_border_skip_all_zero, {
    browser::css::ComputedStyle style;
    browser::css::LayoutNode node(nullptr, style);
    node.content = {0, 0, 100, 50};
    node.border = {0, 0, 0, 0};

    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(&node).sync_wait().unwrap();

    unsigned fill_rect_count = 0;
    for (auto& cmd : _list->commands()) {
        if (cmd.type == browser::render::PaintCommand::Type::FILL_RECT) {
            fill_rect_count++;
        }
    }
    if (fill_rect_count != 0) { _err = "expected 0 FILL_RECT commands"; return false; }
})

TEST(paint_border_edge_sizes, {
    browser::css::ComputedStyle style;
    browser::css::LayoutNode node(nullptr, style);
    node.content = {10, 20, 200, 100};
    node.border = {3, 0, 0, 0};

    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(&node).sync_wait().unwrap();

    unsigned fill_count = 0;
    for (auto& cmd : _list->commands()) {
        if (cmd.type == browser::render::PaintCommand::Type::FILL_RECT) {
            fill_count++;
        }
    }
    // Only 1 FILL_RECT for top border (no background since transparent by default)
    if (fill_count != 1) { _err = "expected 1 FILL_RECT for top border"; return false; }
})

TEST(paint_background_covers_padding_area, {
    // Background must extend through padding to border edge
    browser::css::ComputedStyle style;
    style.properties["background-color"] = make_color_val(255, 0, 0);
    style.properties["overflow"] = overflow_hidden_val();

    browser::css::LayoutNode node(nullptr, style);
    node.content = {0, 0, 100, 100};
    node.padding = {10, 10, 10, 10};
    node.border = {5, 5, 5, 5};

    browser::render::Painter painter(nullptr);
    auto _list = painter.paint_async(&node).sync_wait().unwrap();

    auto& cmds = _list->commands();
    // Background FILL_RECT is first
    if (cmds.size() < 1) { _err = "expected at least 1 command"; return false; }
    if (cmds[0].type != browser::render::PaintCommand::Type::FILL_RECT) { _err = "expected FILL_RECT"; return false; }

    // Background should cover border box: x=0-10=-10, y=0-10=-10, w=100+10+10+5+5=130, h=100+10+10+5+5=130
    if (cmds[0].rect.x != -10) { _err = "background x should be -10, got " + std::to_string(cmds[0].rect.x); return false; }
    if (cmds[0].rect.y != -10) { _err = "background y should be -10"; return false; }
    if (cmds[0].rect.width != 130) { _err = "background width should be 130"; return false; }
    if (cmds[0].rect.height != 130) { _err = "background height should be 130"; return false; }

    // Padding box clip: background + 4 border edges + pushclip = index 5
    if (cmds.size() < 6) { _err = "expected at least 6 commands"; return false; }
    if (cmds[5].type != browser::render::PaintCommand::Type::PUSH_CLIP) { _err = "expected PUSH_CLIP at index 5"; return false; }
    // Padding box: x=0-10=-10, y=0-10=-10, w=100+10+10=120, h=100+10+10=120
    if (cmds[5].rect.x != -10) { _err = "clip x should be -10"; return false; }
    if (cmds[5].rect.y != -10) { _err = "clip y should be -10"; return false; }
    if (cmds[5].rect.width != 120) { _err = "clip width should be 120"; return false; }
    if (cmds[5].rect.height != 120) { _err = "clip height should be 120"; return false; }
})
