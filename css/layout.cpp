#include "layout.hpp"
#include "grid.hpp"
#include "../html/traversal.hpp"
#include "../async/executor.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>
#include <sstream>

namespace browser::css {

namespace {

f32 deg_to_rad(f32 deg) {
    return deg * 3.14159265f / 180.0f;
}

}

// ── Rect::intersect ────────────────────────────────────────────

std::optional<Rect> Rect::intersect(const Rect& o) const {
    f32 l = std::max(x, o.x);
    f32 r = std::min(x + width, o.x + o.width);
    f32 t = std::max(y, o.y);
    f32 b = std::min(y + height, o.y + o.height);
    if (l < r && t < b) {
        return Rect{l, t, r - l, b - t};
    }
    return std::nullopt;
}

// ── LayoutNode constructors ─────────────────────────────────────

LayoutNode::LayoutNode(html::Element* element, ComputedStyle style)
    : node_(element), style_(std::move(style)), is_text_(false) {
    element_key_ = element ? element->tag_name + "_" + element->id() : "";
    // Add a unique suffix if available
    if (element && !element->id().empty()) {
        // Use the element pointer as part of the key to ensure uniqueness
        std::ostringstream ss;
        ss << element;
        element_key_ += ss.str();
    }
}

LayoutNode::LayoutNode(const std::string& text, ComputedStyle style)
    : node_(nullptr), style_(std::move(style)), is_text_(true), text_(text) {}

// ── Box geometry helpers ────────────────────────────────────────

Rect LayoutNode::get_padding_box() const {
    return {
        content.x - padding.left,
        content.y - padding.top,
        content.width + padding.left + padding.right,
        content.height + padding.top + padding.bottom
    };
}

Rect LayoutNode::get_border_box() const {
    auto pb = get_padding_box();
    return {
        pb.x - border.left,
        pb.y - border.top,
        pb.width + border.left + border.right,
        pb.height + border.top + border.bottom
    };
}

Rect LayoutNode::get_margin_box() const {
    auto bb = get_border_box();
    return {
        bb.x - margin.left,
        bb.y - margin.top,
        bb.width + margin.left + margin.right,
        bb.height + margin.top + margin.bottom
    };
}

void LayoutNode::layout(f32, f32) {}

void LayoutNode::set_position(f32 x, f32 y) {
    content.x = x;
    content.y = y;
}

// ── resolve_font_size ───────────────────────────────────────────

f32 LayoutEngine::resolve_font_size(const ComputedStyle& style, f32 parent_font_size) const {
    auto* v = style.get("font-size");
    if (!v) return parent_font_size;

    if (v->type == CSSValue::Type::LENGTH) {
        switch (v->length.unit) {
            case Length::Unit::EM:  return v->length.value * parent_font_size;
            case Length::Unit::REM: return v->length.value * root_font_size_;
            case Length::Unit::PX:  return v->length.value;
            default: return parent_font_size;
        }
    }

    if (v->type == CSSValue::Type::KEYWORD) {
        if (v->keyword == "small")    return 13.0f;
        if (v->keyword == "medium")   return 16.0f;
        if (v->keyword == "large")    return 18.0f;
        if (v->keyword == "x-large")  return 24.0f;
        if (v->keyword == "xx-large") return 32.0f;
    }

    return parent_font_size;
}

// ── calc string evaluation ──────────────────────────────────────

f32 LayoutEngine::resolve_calc_string(const std::string& expr, f32 parent_value, f32 font_size) const {
    // Extract expression from "calc(...)"
    std::string s = expr;
    std::size_t start = s.find("calc(");
    if (start != std::string::npos) {
        s = s.substr(start + 5);
    }
    if (!s.empty() && s.back() == ')') s.pop_back();

    // Simple left-to-right evaluation of "term op term op term" expressions
    // Terms can be: numeric, px, %, em, rem, vw, vh
    auto read_term = [&](std::string& str) -> f32 {
        while (!str.empty() && str[0] == ' ') str = str.substr(1);
        if (str.empty()) return 0;
        char* end = nullptr;
        f32 num = static_cast<f32>(std::strtof(str.c_str(), &end));
        if (end && end != str.c_str()) {
            std::string unit = end;
            // Skip past the number
            ptrdiff_t consumed = end - str.c_str();
            str = str.substr(consumed);
            while (!str.empty() && str[0] == ' ') str = str.substr(1);

            if (unit.empty() || unit[0] == '+' || unit[0] == '-' || unit[0] == '*' || unit[0] == '/') {
                return num; // bare number
            }
            if (unit.substr(0, 2) == "px") { str = str.substr(2); return num; }
            if (unit.substr(0, 1) == "%") { str = str.substr(1); return num / 100.0f * parent_value; }
            if (unit.substr(0, 2) == "em") { str = str.substr(2); return num * font_size; }
            if (unit.substr(0, 3) == "rem") { str = str.substr(3); return num * root_font_size_; }
            if (unit.substr(0, 2) == "vw") { str = str.substr(2); return num / 100.0f * viewport_width_; }
            if (unit.substr(0, 2) == "vh") { str = str.substr(2); return num / 100.0f * viewport_height_; }
        }
        return 0;
    };

    f32 result = 0;
    char last_op = '+';
    while (!s.empty()) {
        while (!s.empty() && s[0] == ' ') s = s.substr(1);
        if (s.empty()) break;

        // Read op
        if (s[0] == '+' || s[0] == '-' || s[0] == '*' || s[0] == '/') {
            last_op = s[0];
            s = s.substr(1);
            continue;
        }

        f32 term = read_term(s);
        switch (last_op) {
            case '+': result += term; break;
            case '-': result -= term; break;
            case '*': result *= term; break;
            case '/': result = (term != 0) ? result / term : result; break;
        }
    }
    return result;
}

// ── resolve_length ──────────────────────────────────────────────

f32 LayoutEngine::resolve_length(const Length& len, f32 parent_value, f32 font_size) const {
    switch (len.unit) {
        case Length::Unit::PX:      return len.value;
        case Length::Unit::EM:      return len.value * font_size;
        case Length::Unit::REM:     return len.value * root_font_size_;
        case Length::Unit::PERCENT: return len.value / 100.0f * parent_value;
        case Length::Unit::VW:      return len.value / 100.0f * viewport_width_;
        case Length::Unit::VH:      return len.value / 100.0f * viewport_height_;
        case Length::Unit::NONE:    return 0.0f;
        case Length::Unit::DEG:     return len.value;
        case Length::Unit::S:       return len.value * 1000.0f; // s to ms
        case Length::Unit::MS:      return len.value;
    }
    return 0.0f;
}

// ── resolve_side_value ──────────────────────────────────────────

f32 LayoutEngine::resolve_side_value(const ComputedStyle& style,
                                      const std::string& side_prop,
                                      const std::string& shorthand_prop,
                                      f32 containing, f32 font_size) const
{
    auto* v = style.get(side_prop);
    if (!v || (v->type == CSSValue::Type::KEYWORD && v->keyword == "auto")) {
        v = style.get(shorthand_prop);
    }
    if (!v || (v->type == CSSValue::Type::KEYWORD && v->keyword == "auto")) return 0.0f;
    if (v->type == CSSValue::Type::LENGTH) {
        return resolve_length(v->length, containing, font_size);
    }
    if (v->type == CSSValue::Type::STRING && v->string_value.find("calc(") != std::string::npos) {
        return resolve_calc_string(v->string_value, containing, font_size);
    }
    return 0.0f;
}

// ── resolve_property ────────────────────────────────────────────

f32 LayoutEngine::resolve_property(const ComputedStyle& style,
                                    const std::string& prop,
                                    f32 containing, f32 font_size) const
{
    auto* v = style.get(prop);
    if (!v || (v->type == CSSValue::Type::KEYWORD && v->keyword == "auto")) return 0.0f;
    if (v->type == CSSValue::Type::LENGTH) {
        return resolve_length(v->length, containing, font_size);
    }
    if (v->type == CSSValue::Type::STRING && !v->string_value.empty() &&
        v->string_value.find("calc(") != std::string::npos) {
        return resolve_calc_string(v->string_value, containing, font_size);
    }
    return 0.0f;
}

// ── Helpers ─────────────────────────────────────────────────────

bool LayoutEngine::is_block_element(const ComputedStyle& style) {
    auto* v = style.get("display");
    if (!v || v->type != CSSValue::Type::KEYWORD) return true;
    return v->keyword == "block" || v->keyword == "flex" || v->keyword == "grid" ||
           v->keyword == "list-item" || v->keyword == "table";
}

bool LayoutEngine::is_inline_element(const ComputedStyle& style) {
    auto* v = style.get("display");
    if (!v || v->type != CSSValue::Type::KEYWORD) return false;
    return v->keyword == "inline" || v->keyword == "inline-block";
}

bool LayoutEngine::is_positioned(const ComputedStyle& style) {
    auto* v = style.get("position");
    if (!v || v->type != CSSValue::Type::KEYWORD) return false;
    return v->keyword != "static";
}

bool LayoutEngine::is_fixed_or_sticky(const ComputedStyle& style) {
    auto* v = style.get("position");
    if (!v || v->type != CSSValue::Type::KEYWORD) return false;
    return v->keyword == "fixed" || v->keyword == "sticky";
}

f32 LayoutEngine::get_z_index(const ComputedStyle& style) {
    auto* v = style.get("z-index");
    if (v && v->type == CSSValue::Type::NUMBER) return v->number;
    if (v && v->type == CSSValue::Type::KEYWORD && v->keyword == "auto") return 0;
    return 0;
}

bool LayoutEngine::creates_stacking_context(const ComputedStyle& style) {
    // z-index != auto
    auto* zi = style.get("z-index");
    if (zi && zi->type == CSSValue::Type::NUMBER) return true;

    // opacity < 1
    auto* op = style.get("opacity");
    if (op && op->type == CSSValue::Type::NUMBER && op->number < 1.0f) return true;

    // transform != none
    auto* tr = style.get("transform");
    if (tr && tr->type == CSSValue::Type::TRANSFORM && !tr->transforms.empty()) return true;

    // position: relative/absolute/fixed/sticky with z-index auto
    auto* pos = style.get("position");
    if (pos && pos->type == CSSValue::Type::KEYWORD &&
        (pos->keyword == "fixed" || pos->keyword == "sticky")) return true;

    return false;
}

LayoutNode* LayoutEngine::find_positioned_ancestor(LayoutNode* node) {
    LayoutNode* ancestor = node->parent;
    while (ancestor) {
        if (is_positioned(ancestor->style())) return ancestor;
        ancestor = ancestor->parent;
    }
    return nullptr;
}

bool LayoutEngine::is_flex_element(const ComputedStyle& style) {
    auto* v = style.get("display");
    if (!v || v->type != CSSValue::Type::KEYWORD) return false;
    return v->keyword == "flex" || v->keyword == "inline-flex";
}

bool LayoutEngine::is_grid_element(const ComputedStyle& style) {
    auto* v = style.get("display");
    if (!v || v->type != CSSValue::Type::KEYWORD) return false;
    return v->keyword == "grid" || v->keyword == "inline-grid";
}

// ── Transform helpers ───────────────────────────────────────────

void apply_transform_to_node(LayoutNode* node) {
    auto* tr = node->style().get("transform");
    if (!tr || tr->type != CSSValue::Type::TRANSFORM || tr->transforms.empty()) return;

    // Apply transform-origin
    auto* to = node->style().get("transform-origin");
    f32 origin_x = node->content.width / 2.0f;
    f32 origin_y = node->content.height / 2.0f;
    if (to && to->type == CSSValue::Type::STRING) {
        // Simplified: try to parse "Xpx Ypx" or percentages
        std::string s = to->string_value;
        char* end = nullptr;
        f32 v1 = std::strtof(s.c_str(), &end);
        if (end && end != s.c_str()) {
            origin_x = v1;
            std::string rest = end;
            while (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
            f32 v2 = std::strtof(rest.c_str(), &end);
            if (end && end != rest.c_str()) {
                origin_y = v2;
            }
        } else {
            // Keyword-based
            if (s.find("left") != std::string::npos) origin_x = 0;
            else if (s.find("right") != std::string::npos) origin_x = node->content.width;
            else origin_x = node->content.width / 2;

            if (s.find("top") != std::string::npos) origin_y = 0;
            else if (s.find("bottom") != std::string::npos) origin_y = node->content.height;
            else origin_y = node->content.height / 2;
        }
    }

    node->transform_matrix = Mat3x3::identity();
    node->transform_matrix = node->transform_matrix.translate(origin_x, origin_y);

    for (const auto& tf : tr->transforms) {
        f32 a = tf.args.size() > 0 ? tf.args[0] : 0;
        f32 b = tf.args.size() > 1 ? tf.args[1] : 0;
        f32 c = tf.args.size() > 2 ? tf.args[2] : 0;
        f32 d = tf.args.size() > 3 ? tf.args[3] : 0;
        f32 e = tf.args.size() > 4 ? tf.args[4] : 0;
        f32 f = tf.args.size() > 5 ? tf.args[5] : 0;

        switch (tf.type) {
            case TransformFunc::Type::TRANSLATE:
                node->transform_matrix = node->transform_matrix.translate(a, b);
                break;
            case TransformFunc::Type::TRANSLATE_X:
                node->transform_matrix = node->transform_matrix.translate(a, 0);
                break;
            case TransformFunc::Type::TRANSLATE_Y:
                node->transform_matrix = node->transform_matrix.translate(0, a);
                break;
            case TransformFunc::Type::SCALE:
                node->transform_matrix = node->transform_matrix.scale(a, b > 0 ? b : a);
                break;
            case TransformFunc::Type::SCALE_X:
                node->transform_matrix = node->transform_matrix.scale(a, 1);
                break;
            case TransformFunc::Type::SCALE_Y:
                node->transform_matrix = node->transform_matrix.scale(1, a);
                break;
            case TransformFunc::Type::ROTATE:
                node->transform_matrix = node->transform_matrix.rotate(deg_to_rad(a));
                break;
            case TransformFunc::Type::SKEW:
                node->transform_matrix = node->transform_matrix.skew(deg_to_rad(a), deg_to_rad(b));
                break;
            case TransformFunc::Type::SKEW_X:
                node->transform_matrix = node->transform_matrix.skew(deg_to_rad(a), 0);
                break;
            case TransformFunc::Type::SKEW_Y:
                node->transform_matrix = node->transform_matrix.skew(0, deg_to_rad(a));
                break;
            case TransformFunc::Type::MATRIX:
                node->transform_matrix = node->transform_matrix.multiply(Mat3x3::from_values(a, b, c, d, e, f));
                break;
        }
    }

    node->transform_matrix = node->transform_matrix.translate(-origin_x, -origin_y);
    node->has_transform = true;
}

// ── Flex helpers ────────────────────────────────────────────────

// (all flex helpers remain the same as before, copied from original)
// ── Part 1: resolve_flex_config ─────────────────────────────────

FlexConfig LayoutEngine::resolve_flex_config(const ComputedStyle& style, f32 font_size) const {
    FlexConfig cfg;

    auto* dir = style.get("flex-direction");
    if (dir && dir->type == CSSValue::Type::KEYWORD) {
        if (dir->keyword == "row-reverse") cfg.direction = FlexDirection::ROW_REVERSE;
        else if (dir->keyword == "column") cfg.direction = FlexDirection::COLUMN;
        else if (dir->keyword == "column-reverse") cfg.direction = FlexDirection::COLUMN_REVERSE;
    }

    auto* wrap = style.get("flex-wrap");
    if (wrap && wrap->type == CSSValue::Type::KEYWORD) {
        if (wrap->keyword == "wrap") cfg.wrap = FlexWrap::WRAP;
        else if (wrap->keyword == "wrap-reverse") cfg.wrap = FlexWrap::WRAP_REVERSE;
    }

    auto* jc = style.get("justify-content");
    if (jc && jc->type == CSSValue::Type::KEYWORD) {
        if (jc->keyword == "flex-end") cfg.justify_content = JustifyContent::FLEX_END;
        else if (jc->keyword == "center") cfg.justify_content = JustifyContent::CENTER;
        else if (jc->keyword == "space-between") cfg.justify_content = JustifyContent::SPACE_BETWEEN;
        else if (jc->keyword == "space-around") cfg.justify_content = JustifyContent::SPACE_AROUND;
        else if (jc->keyword == "space-evenly") cfg.justify_content = JustifyContent::SPACE_EVENLY;
    }

    auto* ai = style.get("align-items");
    if (ai && ai->type == CSSValue::Type::KEYWORD) {
        if (ai->keyword == "flex-start") cfg.align_items = AlignItems::FLEX_START;
        else if (ai->keyword == "flex-end") cfg.align_items = AlignItems::FLEX_END;
        else if (ai->keyword == "center") cfg.align_items = AlignItems::CENTER;
        else if (ai->keyword == "baseline") cfg.align_items = AlignItems::BASELINE;
    }

    auto* ac = style.get("align-content");
    if (ac && ac->type == CSSValue::Type::KEYWORD) {
        if (ac->keyword == "flex-start") cfg.align_content = AlignContent::FLEX_START;
        else if (ac->keyword == "flex-end") cfg.align_content = AlignContent::FLEX_END;
        else if (ac->keyword == "center") cfg.align_content = AlignContent::CENTER;
        else if (ac->keyword == "space-between") cfg.align_content = AlignContent::SPACE_BETWEEN;
        else if (ac->keyword == "space-around") cfg.align_content = AlignContent::SPACE_AROUND;
    }

    auto* gap = style.get("gap");
    if (gap && gap->type == CSSValue::Type::LENGTH) {
        f32 g = resolve_length(gap->length, 0, font_size);
        cfg.row_gap = g;
        cfg.column_gap = g;
    }

    auto* rg = style.get("row-gap");
    if (rg && rg->type == CSSValue::Type::LENGTH) {
        cfg.row_gap = resolve_length(rg->length, 0, font_size);
    }

    auto* cg = style.get("column-gap");
    if (cg && cg->type == CSSValue::Type::LENGTH) {
        cfg.column_gap = resolve_length(cg->length, 0, font_size);
    }

    return cfg;
}

FlexItem LayoutEngine::resolve_flex_item(LayoutNode* child, const FlexConfig& config,
                                          f32 containing_main, f32 containing_cross,
                                          f32 font_size) const
{
    FlexItem item;
    item.node = child;

    auto* fg = child->style().get("flex-grow");
    if (fg && fg->type == CSSValue::Type::NUMBER) item.flex_grow = fg->number;

    auto* fs = child->style().get("flex-shrink");
    if (fs && fs->type == CSSValue::Type::NUMBER) item.flex_shrink = fs->number;

    auto* fb = child->style().get("flex-basis");
    if (fb) {
        if (fb->type == CSSValue::Type::LENGTH) {
            item.flex_basis = resolve_length(fb->length, containing_main, font_size);
        }
    }

    auto* as = child->style().get("align-self");
    if (as && as->type == CSSValue::Type::KEYWORD) {
        if (as->keyword == "stretch") item.align_self = AlignSelf::STRETCH;
        else if (as->keyword == "flex-start") item.align_self = AlignSelf::FLEX_START;
        else if (as->keyword == "flex-end") item.align_self = AlignSelf::FLEX_END;
        else if (as->keyword == "center") item.align_self = AlignSelf::CENTER;
        else if (as->keyword == "baseline") item.align_self = AlignSelf::BASELINE;
    }

    bool is_row = config.direction == FlexDirection::ROW ||
                  config.direction == FlexDirection::ROW_REVERSE;

    EdgeSizes margins;
    margins.left = resolve_side_value(child->style(), "margin-left", "margin", containing_main, font_size);
    margins.right = resolve_side_value(child->style(), "margin-right", "margin", containing_main, font_size);
    margins.top = resolve_side_value(child->style(), "margin-top", "margin", containing_cross, font_size);
    margins.bottom = resolve_side_value(child->style(), "margin-bottom", "margin", containing_cross, font_size);
    child->margin = margins;

    if (is_row) {
        item.main_margin_start = margins.left;
        item.main_margin_end = margins.right;
        item.cross_margin_start = margins.top;
        item.cross_margin_end = margins.bottom;
    } else {
        item.main_margin_start = margins.top;
        item.main_margin_end = margins.bottom;
        item.cross_margin_start = margins.left;
        item.cross_margin_end = margins.right;
    }

    EdgeSizes borders, paddings;
    borders.left = resolve_side_value(child->style(), "border-left-width", "border-width", containing_main, font_size);
    borders.right = resolve_side_value(child->style(), "border-right-width", "border-width", containing_main, font_size);
    borders.top = resolve_side_value(child->style(), "border-top-width", "border-width", containing_cross, font_size);
    borders.bottom = resolve_side_value(child->style(), "border-bottom-width", "border-width", containing_cross, font_size);
    paddings.left = resolve_side_value(child->style(), "padding-left", "padding", containing_main, font_size);
    paddings.right = resolve_side_value(child->style(), "padding-right", "padding", containing_main, font_size);
    paddings.top = resolve_side_value(child->style(), "padding-top", "padding", containing_cross, font_size);
    paddings.bottom = resolve_side_value(child->style(), "padding-bottom", "padding", containing_cross, font_size);

    child->border = borders;
    child->padding = paddings;

    f32 bp_main_start, bp_main_end, bp_cross_start, bp_cross_end;
    if (is_row) {
        bp_main_start = borders.left + paddings.left;
        bp_main_end = borders.right + paddings.right;
        bp_cross_start = borders.top + paddings.top;
        bp_cross_end = borders.bottom + paddings.bottom;
    } else {
        bp_main_start = borders.top + paddings.top;
        bp_main_end = borders.bottom + paddings.bottom;
        bp_cross_start = borders.left + paddings.left;
        bp_cross_end = borders.right + paddings.right;
    }

    item.main_border_padding = bp_main_start + bp_main_end;
    item.cross_border_padding = bp_cross_start + bp_cross_end;

    if (item.flex_basis >= 0) {
        item.base_size = item.flex_basis;
    } else {
        auto* size_prop = child->style().get(is_row ? "width" : "height");
        if (size_prop && size_prop->type == CSSValue::Type::LENGTH) {
            item.base_size = resolve_length(size_prop->length,
                                            is_row ? containing_main : containing_cross,
                                            font_size);
        }
    }

    item.hypothetical_main = item.base_size + item.main_border_padding;
    return item;
}

static bool is_row_direction(FlexDirection d) {
    return d == FlexDirection::ROW || d == FlexDirection::ROW_REVERSE;
}

static bool is_reverse_direction(FlexDirection d) {
    return d == FlexDirection::ROW_REVERSE || d == FlexDirection::COLUMN_REVERSE;
}

std::vector<FlexLine> LayoutEngine::distribute_lines(
    std::vector<FlexItem>& items, const FlexConfig& config,
    f32 container_main) const
{
    std::vector<FlexLine> lines;

    if (config.wrap == FlexWrap::NOWRAP || items.empty()) {
        FlexLine line;
        for (auto& item : items) line.items.push_back(&item);
        lines.push_back(std::move(line));
    } else {
        f32 gap = is_row_direction(config.direction) ? config.column_gap : config.row_gap;
        FlexLine current;
        f32 current_main = 0;

        for (auto& item : items) {
            f32 item_main = item.hypothetical_main;
            if (!current.items.empty() && current_main + item_main > container_main) {
                lines.push_back(std::move(current));
                current = FlexLine();
                current_main = 0;
            }
            current.items.push_back(&item);
            current_main += item_main;
            if (current.items.size() > 1) current_main += gap;
        }

        if (!current.items.empty()) {
            lines.push_back(std::move(current));
        }

        if (config.wrap == FlexWrap::WRAP_REVERSE) {
            std::reverse(lines.begin(), lines.end());
        }
    }

    return lines;
}

void LayoutEngine::distribute_free_space(FlexLine& line, f32 container_main,
                                          const FlexConfig& config) const
{
    if (line.items.empty()) return;

    f32 gap = is_row_direction(config.direction) ? config.column_gap : config.row_gap;
    f32 total_hypothetical = 0;
    for (auto* item : line.items) {
        total_hypothetical += item->hypothetical_main;
    }
    total_hypothetical += gap * static_cast<f32>(line.items.size() - 1);
    f32 free_space = container_main - total_hypothetical;

    if (free_space > 0) {
        f32 total_grow = 0;
        for (auto* item : line.items) total_grow += item->flex_grow;
        if (total_grow > 0) {
            for (auto* item : line.items) {
                item->target_main = item->hypothetical_main +
                                    free_space * (item->flex_grow / total_grow);
            }
        } else {
            for (auto* item : line.items) item->target_main = item->hypothetical_main;
        }
    } else if (free_space < 0) {
        f32 total_scaled_shrink = 0;
        for (auto* item : line.items) {
            total_scaled_shrink += item->flex_shrink * item->hypothetical_main;
        }
        if (total_scaled_shrink > 0) {
            for (auto* item : line.items) {
                f32 scaled = item->flex_shrink * item->hypothetical_main;
                item->target_main = item->hypothetical_main +
                                    free_space * (scaled / total_scaled_shrink);
                if (item->target_main < item->main_border_padding) {
                    item->target_main = item->main_border_padding;
                }
            }
        } else {
            for (auto* item : line.items) item->target_main = item->hypothetical_main;
        }
    } else {
        for (auto* item : line.items) item->target_main = item->hypothetical_main;
    }

    if (is_reverse_direction(config.direction)) {
        std::reverse(line.items.begin(), line.items.end());
    }
}

void LayoutEngine::compute_cross_sizes(FlexLine& line, f32 container_cross,
                                        const FlexConfig& config, f32 font_size)
{
    if (line.items.empty()) return;

    bool is_row = is_row_direction(config.direction);

    for (auto* item : line.items) {
        AlignSelf effective_align = item->align_self;
        if (effective_align == AlignSelf::AUTO) {
            effective_align = static_cast<AlignSelf>(
                static_cast<int>(config.align_items));
        }

        if (effective_align == AlignSelf::STRETCH) {
            auto* cross_size_prop = item->node->style().get(is_row ? "height" : "width");
            if (cross_size_prop && cross_size_prop->type == CSSValue::Type::LENGTH) {
                item->cross_size = resolve_length(cross_size_prop->length,
                                                   container_cross, font_size);
            } else {
                item->cross_size = container_cross - item->cross_margin_start -
                                   item->cross_margin_end - item->cross_border_padding;
                if (item->cross_size < 0) item->cross_size = 0;
            }
        } else {
            auto* cross_size_prop = item->node->style().get(is_row ? "height" : "width");
            if (cross_size_prop && cross_size_prop->type == CSSValue::Type::LENGTH) {
                item->cross_size = resolve_length(cross_size_prop->length,
                                                   container_cross, font_size);
            } else {
                f32 main_content = item->target_main - item->main_border_padding;
                if (is_row) {
                    item->node->content.width = main_content;
                    if (is_flex_element(item->node->style())) {
                        layout_flex(item->node, main_content, container_cross);
                    } else {
                        layout_children(item->node, main_content, container_cross);
                    }
                    item->cross_size = item->node->content.height;
                } else {
                    item->node->content.height = main_content;
                    if (is_flex_element(item->node->style())) {
                        layout_flex(item->node, container_cross, main_content);
                    } else {
                        layout_children(item->node, container_cross, main_content);
                    }
                    item->cross_size = item->node->content.width;
                }
            }
        }
    }

    f32 max_cross = 0;
    for (auto* item : line.items) {
        f32 total = item->cross_size + item->cross_margin_start +
                    item->cross_margin_end + item->cross_border_padding;
        if (total > max_cross) max_cross = total;
    }
    line.cross_size = max_cross;
}

void LayoutEngine::align_lines(std::vector<FlexLine>& lines, f32 container_cross,
                                const FlexConfig& config) const
{
    if (lines.empty()) return;

    f32 gap = is_row_direction(config.direction) ? config.row_gap : config.column_gap;

    f32 total_cross = 0;
    for (auto& line : lines) {
        total_cross += line.cross_size;
    }
    total_cross += gap * static_cast<f32>(lines.size() - 1);
    f32 free_cross = container_cross - total_cross;

    f32 initial_offset = 0;
    f32 gap_extra = gap;

    switch (config.align_content) {
        case AlignContent::FLEX_START:
            initial_offset = 0;
            gap_extra = gap;
            break;
        case AlignContent::FLEX_END:
            initial_offset = free_cross;
            gap_extra = gap;
            break;
        case AlignContent::CENTER:
            initial_offset = free_cross / 2;
            gap_extra = gap;
            break;
        case AlignContent::SPACE_BETWEEN:
            initial_offset = 0;
            if (lines.size() > 1) gap_extra = free_cross / static_cast<f32>(lines.size() - 1) + gap;
            else gap_extra = 0;
            break;
        case AlignContent::SPACE_AROUND: {
            f32 spacing = free_cross / static_cast<f32>(lines.size() * 2);
            initial_offset = spacing;
            gap_extra = spacing * 2 + gap;
            break;
        }
        case AlignContent::STRETCH: {
            if (lines.size() > 0 && free_cross > 0) {
                f32 extra = free_cross / static_cast<f32>(lines.size());
                for (auto& line : lines) line.cross_size += extra;
            }
            initial_offset = 0;
            gap_extra = gap;
            break;
        }
    }

    f32 cross_pos = initial_offset;
    for (auto& line : lines) {
        for (auto* item : line.items) {
            item->cross_offset = cross_pos;
            AlignSelf effective = item->align_self;
            if (effective == AlignSelf::AUTO) {
                effective = static_cast<AlignSelf>(static_cast<int>(config.align_items));
            }
            if (effective != AlignSelf::STRETCH) {
                f32 line_cross = line.cross_size;
                f32 item_cross = item->cross_size + item->cross_border_padding +
                                 item->cross_margin_start + item->cross_margin_end;
                f32 extra = line_cross - item_cross;
                if (extra > 0) {
                    if (effective == AlignSelf::CENTER) {
                        item->cross_offset += extra / 2 + item->cross_margin_start;
                    } else if (effective == AlignSelf::FLEX_END) {
                        item->cross_offset += extra + item->cross_margin_start;
                    } else {
                        item->cross_offset += item->cross_margin_start;
                    }
                } else {
                    item->cross_offset += item->cross_margin_start;
                }
            } else {
                item->cross_offset += item->cross_margin_start;
            }
        }
        cross_pos += line.cross_size + gap_extra;
    }
}

// ── Flex layout ─────────────────────────────────────────────────

void LayoutEngine::layout_flex(LayoutNode* node, f32 containing_width, f32 containing_height) {
    if (!node) return;

    f32 parent_font_size = root_font_size_;
    if (node->parent) {
        auto* pfs = node->parent->style().get("font-size");
        if (pfs && pfs->type == CSSValue::Type::LENGTH && pfs->length.unit == Length::Unit::PX) {
            parent_font_size = pfs->length.value;
        }
    }
    f32 font_size = resolve_font_size(node->style(), parent_font_size);

    FlexConfig config = resolve_flex_config(node->style(), font_size);

    bool is_row = is_row_direction(config.direction);

    f32 container_main;
    if (is_row) {
        auto* wv = node->style().get("width");
        if (wv && wv->type == CSSValue::Type::LENGTH) {
            container_main = resolve_length(wv->length, containing_width, font_size);
        } else {
            container_main = node->content.width;
        }
    } else {
        auto* hv = node->style().get("height");
        if (hv && hv->type == CSSValue::Type::LENGTH) {
            container_main = resolve_length(hv->length, containing_height, font_size);
        } else {
            container_main = containing_height;
        }
    }

    f32 container_cross;
    if (is_row) {
        auto* hv = node->style().get("height");
        if (hv && hv->type == CSSValue::Type::LENGTH) {
            container_cross = resolve_length(hv->length, containing_height, font_size);
        } else {
            container_cross = containing_height;
        }
    } else {
        auto* wv = node->style().get("width");
        if (wv && wv->type == CSSValue::Type::LENGTH) {
            container_cross = resolve_length(wv->length, containing_width, font_size);
        } else {
            container_cross = node->content.width;
        }
    }

    std::vector<FlexItem> items;
    for (auto& child : node->children) {
        auto* pos = child->style().get("position");
        bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
        if (is_absolute) continue;

        f32 containing_main = container_main;
        f32 containing_cross = container_cross;
        items.push_back(resolve_flex_item(child.get(), config, containing_main, containing_cross, font_size));
    }

    std::stable_sort(items.begin(), items.end(), [](const FlexItem& a, const FlexItem& b) {
        auto* ao = a.node->style().get("order");
        auto* bo = b.node->style().get("order");
        f32 av = (ao && ao->type == CSSValue::Type::NUMBER) ? ao->number : 0;
        f32 bv = (bo && bo->type == CSSValue::Type::NUMBER) ? bo->number : 0;
        return av < bv;
    });

    std::vector<FlexLine> lines = distribute_lines(items, config, container_main);

    for (auto& line : lines) {
        distribute_free_space(line, container_main, config);
    }

    for (auto& line : lines) {
        compute_cross_sizes(line, container_cross, config, font_size);
    }

    align_lines(lines, container_cross, config);

    f32 gap = is_row ? config.column_gap : config.row_gap;

    for (auto& line : lines) {
        f32 total_target = 0;
        for (auto* item : line.items) total_target += item->target_main;
        total_target += gap * static_cast<f32>(line.items.size() - 1);
        f32 free_space = container_main - total_target;

        f32 initial_offset = 0;
        f32 gap_extra = gap;

        switch (config.justify_content) {
            case JustifyContent::FLEX_START:
                initial_offset = 0;
                gap_extra = gap;
                break;
            case JustifyContent::FLEX_END:
                initial_offset = free_space;
                gap_extra = gap;
                break;
            case JustifyContent::CENTER:
                initial_offset = free_space / 2;
                gap_extra = gap;
                break;
            case JustifyContent::SPACE_BETWEEN:
                initial_offset = 0;
                if (line.items.size() > 1) gap_extra = free_space / static_cast<f32>(line.items.size() - 1) + gap;
                else gap_extra = 0;
                break;
            case JustifyContent::SPACE_AROUND: {
                f32 spacing = free_space / static_cast<f32>(line.items.size());
                initial_offset = spacing / 2;
                gap_extra = spacing + gap;
                break;
            }
            case JustifyContent::SPACE_EVENLY: {
                f32 spacing = free_space / static_cast<f32>(line.items.size() + 1);
                initial_offset = spacing;
                gap_extra = spacing + gap;
                break;
            }
        }

        f32 main_pos = initial_offset;
        for (auto* item : line.items) {
            f32 main_start = main_pos;
            f32 main_end = main_start + item->target_main;

            if (is_row) {
                item->node->content.x = main_start + item->main_margin_start
                                       + item->node->border.left + item->node->padding.left;
                item->node->content.y = item->cross_offset + item->cross_margin_start
                                       + item->node->border.top + item->node->padding.top;
            } else {
                item->node->content.x = item->cross_offset + item->cross_margin_start
                                       + item->node->border.left + item->node->padding.left;
                item->node->content.y = main_start + item->main_margin_start
                                       + item->node->border.top + item->node->padding.top;
            }

            item->node->content.width = is_row ? item->target_main - item->main_border_padding : item->cross_size;
            item->node->content.height = is_row ? item->cross_size : item->target_main - item->main_border_padding;

            main_pos = main_end + gap_extra;
        }
    }

    for (auto& item : items) {
        if (item.node->children.empty()) continue;
        f32 cw = item.node->content.width;
        f32 ch = item.node->content.height;
        if (is_flex_element(item.node->style())) {
            f32 saved_x = item.node->content.x;
            f32 saved_y = item.node->content.y;
            layout_flex(item.node, cw, ch);
            item.node->content.x = saved_x;
            item.node->content.y = saved_y;
            item.node->content.width = cw;
            item.node->content.height = ch;
        } else {
            layout_children(item.node, cw, ch);
        }
    }

    if (is_row) {
        auto* hv = node->style().get("height");
        if (!hv || (hv->type == CSSValue::Type::KEYWORD && hv->keyword == "auto")) {
            f32 max_y = 0;
            for (auto& line : lines) {
                for (auto* item : line.items) {
                    f32 bottom = item->node->content.y + item->node->content.height +
                                 item->node->padding.bottom + item->node->border.bottom +
                                 item->node->margin.bottom;
                    if (bottom > max_y) max_y = bottom;
                }
            }
            node->content.height = max_y;
        }
    } else {
        auto* wv = node->style().get("width");
        if (!wv || (wv->type == CSSValue::Type::KEYWORD && wv->keyword == "auto")) {
            f32 max_x = 0;
            for (auto& line : lines) {
                for (auto* item : line.items) {
                    f32 right = item->node->content.x + item->node->content.width +
                                item->node->padding.right + item->node->border.right +
                                item->node->margin.right;
                    if (right > max_x) max_x = right;
                }
            }
            node->content.width = max_x;
        }
    }
}

// ── Anonymous block factory ─────────────────────────────────────

std::unique_ptr<LayoutNode> LayoutEngine::make_anonymous_block(ComputedStyle style) {
    auto node = std::make_unique<LayoutNode>(static_cast<html::Element*>(nullptr), std::move(style));
    return node;
}

static bool is_display_none(const ComputedStyle& style) {
    auto* v = style.get("display");
    return v && v->type == CSSValue::Type::KEYWORD && v->keyword == "none";
}

// ── build_layout_tree ───────────────────────────────────────────

std::unique_ptr<LayoutNode> LayoutEngine::build_layout_tree(
    html::Node* node,
    std::unordered_map<const html::Element*, ComputedStyle>& styles)
{
    if (!node) return nullptr;

    if (node->type == html::NodeType::ELEMENT) {
        auto* el = static_cast<html::Element*>(node);
        auto it = styles.find(el);
        if (it == styles.end()) return nullptr;
        if (is_display_none(it->second)) return nullptr;

        auto layout_node = std::make_unique<LayoutNode>(el, it->second);

        std::vector<std::unique_ptr<LayoutNode>> inline_pending;

        for (auto& child : node->children) {
            if (child->type == html::NodeType::ELEMENT) {
                auto* child_el = static_cast<html::Element*>(child.get());
                auto child_it = styles.find(child_el);
                if (child_it == styles.end()) continue;
                if (is_display_none(child_it->second)) continue;

                bool child_is_block = is_block_element(child_it->second);
                bool child_is_inline_block = is_inline_element(child_it->second);

                if (child_is_block && !child_is_inline_block) {
                    if (!inline_pending.empty()) {
                        auto anon = make_anonymous_block(it->second);
                        for (auto& r : inline_pending) {
                            r->parent = anon.get();
                            anon->children.push_back(std::move(r));
                        }
                        anon->parent = layout_node.get();
                        layout_node->children.push_back(std::move(anon));
                        inline_pending.clear();
                    }

                    auto child_node = build_layout_tree(child.get(), styles);
                    if (child_node) {
                        child_node->parent = layout_node.get();
                        layout_node->children.push_back(std::move(child_node));
                    }
                } else {
                    auto child_node = build_layout_tree(child.get(), styles);
                    if (child_node) {
                        inline_pending.push_back(std::move(child_node));
                    }
                }
            } else if (child->type == html::NodeType::TEXT) {
                auto* text = static_cast<html::Text*>(child.get());
                bool all_space = true;
                for (char c : text->data) {
                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                        all_space = false;
                        break;
                    }
                }
                if (all_space) continue;

                ComputedStyle text_style = it->second;
                auto text_node = std::make_unique<LayoutNode>(text->data, std::move(text_style));
                inline_pending.push_back(std::move(text_node));
            }
        }

        if (!inline_pending.empty()) {
            if (!layout_node->children.empty() && is_block_element(it->second)) {
                auto anon = make_anonymous_block(it->second);
                for (auto& r : inline_pending) {
                    r->parent = anon.get();
                    anon->children.push_back(std::move(r));
                }
                anon->parent = layout_node.get();
                layout_node->children.push_back(std::move(anon));
            } else {
                for (auto& r : inline_pending) {
                    r->parent = layout_node.get();
                    layout_node->children.push_back(std::move(r));
                }
            }
            inline_pending.clear();
        }

        return layout_node;
    }

    if (node->type == html::NodeType::TEXT) {
        auto* text = static_cast<html::Text*>(node);
        bool all_space = true;
        for (char c : text->data) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                all_space = false;
                break;
            }
        }
        if (all_space) return nullptr;

        ComputedStyle parent_style;
        html::Node* p = node->parent;
        while (p) {
            if (p->type == html::NodeType::ELEMENT) {
                auto* pel = static_cast<html::Element*>(p);
                auto it = styles.find(pel);
                if (it != styles.end()) {
                    parent_style = it->second;
                    break;
                }
            }
            p = p->parent;
        }

        return std::make_unique<LayoutNode>(text->data, std::move(parent_style));
    }

    return nullptr;
}

// ── layout_block ────────────────────────────────────────────────

void LayoutEngine::layout_block(LayoutNode* node, f32 containing_width, f32 containing_height) {
    if (!node) return;

    f32 parent_font_size = root_font_size_;
    if (node->parent) {
        auto* pfs = node->parent->style().get("font-size");
        if (pfs && pfs->type == CSSValue::Type::LENGTH && pfs->length.unit == Length::Unit::PX) {
            parent_font_size = pfs->length.value;
        }
    }
    f32 font_size = resolve_font_size(node->style(), parent_font_size);

    // Check box-sizing
    bool border_box = false;
    auto* bs = node->style().get("box-sizing");
    if (bs && bs->type == CSSValue::Type::KEYWORD && bs->keyword == "border-box") {
        border_box = true;
    }

    bool width_auto = false;
    f32 width = 0;
    auto* wv = node->style().get("width");
    if (!wv || (wv->type == CSSValue::Type::KEYWORD && wv->keyword == "auto")) {
        width_auto = true;
    } else if (wv->type == CSSValue::Type::LENGTH) {
        width = resolve_length(wv->length, containing_width, font_size);
    }

    EdgeSizes margins;
    margins.top = resolve_side_value(node->style(), "margin-top", "margin",
                                     containing_width, font_size);
    margins.bottom = resolve_side_value(node->style(), "margin-bottom", "margin",
                                        containing_width, font_size);
    margins.left = resolve_side_value(node->style(), "margin-left", "margin",
                                      containing_width, font_size);
    margins.right = resolve_side_value(node->style(), "margin-right", "margin",
                                       containing_width, font_size);

    EdgeSizes paddings;
    paddings.top = resolve_side_value(node->style(), "padding-top", "padding",
                                      containing_width, font_size);
    paddings.bottom = resolve_side_value(node->style(), "padding-bottom", "padding",
                                         containing_width, font_size);
    paddings.left = resolve_side_value(node->style(), "padding-left", "padding",
                                       containing_width, font_size);
    paddings.right = resolve_side_value(node->style(), "padding-right", "padding",
                                        containing_width, font_size);

    EdgeSizes borders;
    borders.top = resolve_side_value(node->style(), "border-top-width", "border-width",
                                     containing_width, font_size);
    borders.bottom = resolve_side_value(node->style(), "border-bottom-width", "border-width",
                                        containing_width, font_size);
    borders.left = resolve_side_value(node->style(), "border-left-width", "border-width",
                                      containing_width, font_size);
    borders.right = resolve_side_value(node->style(), "border-right-width", "border-width",
                                       containing_width, font_size);

    if (borders.top == 0) {
        auto* bv = node->style().get("border");
        if (bv && bv->type == CSSValue::Type::LENGTH) {
            f32 bw = resolve_length(bv->length, containing_width, font_size);
            borders = {bw, bw, bw, bw};
        }
    }

    node->margin = margins;
    node->padding = paddings;
    node->border = borders;

    f32 h_padding = paddings.left + paddings.right;
    f32 h_border = borders.left + borders.right;
    f32 h_margin = margins.left + margins.right;

    if (width_auto) {
        if (border_box) {
            node->content.width = containing_width - h_margin;
        } else {
            node->content.width = containing_width - h_margin - h_padding - h_border;
        }
        if (node->content.width < 0) node->content.width = 0;
    } else {
        if (border_box) {
            // width includes padding + border, so content width = width - padding - border
            node->content.width = width - h_padding - h_border;
            if (node->content.width < 0) node->content.width = 0;
        } else {
            node->content.width = width;
        }
    }

    // Apply max-width / min-width
    auto* maxw = node->style().get("max-width");
    bool had_maxw = false;
    if (maxw && maxw->type == CSSValue::Type::LENGTH) {
        f32 mw = resolve_length(maxw->length, containing_width, font_size);
        if (node->content.width > mw) { node->content.width = mw; had_maxw = true; }
    }
    auto* minw = node->style().get("min-width");
    if (minw && minw->type == CSSValue::Type::LENGTH) {
        f32 mw = resolve_length(minw->length, containing_width, font_size);
        if (node->content.width < mw) node->content.width = mw;
    }

    // Auto margins for centering
    auto* ml = node->style().get("margin-left");
    auto* mr = node->style().get("margin-right");
    bool ml_auto = !ml || (ml->type == CSSValue::Type::KEYWORD && ml->keyword == "auto");
    bool mr_auto = !mr || (mr->type == CSSValue::Type::KEYWORD && mr->keyword == "auto");
    bool has_fixed_width = wv && wv->type == CSSValue::Type::LENGTH;
    if (ml_auto && mr_auto && (has_fixed_width || had_maxw)) {
        f32 used_w = node->content.width + h_padding + h_border;
        f32 remaining = containing_width - used_w;
        if (remaining > 0) {
            f32 half = remaining / 2.0f;
            margins.left = half;
            margins.right = half;
        }
    }
    node->margin = margins;

    if (is_grid_element(node->style())) {
        layout_grid(node, containing_width, containing_height);
        // Apply min/max height
        auto* maxh = node->style().get("max-height");
        if (maxh && maxh->type == CSSValue::Type::LENGTH) {
            f32 mh = resolve_length(maxh->length, containing_height, font_size);
            if (node->content.height > mh) node->content.height = mh;
        }
        auto* minh = node->style().get("min-height");
        if (minh && minh->type == CSSValue::Type::LENGTH) {
            f32 mh = resolve_length(minh->length, containing_height, font_size);
            if (node->content.height < mh) node->content.height = mh;
        }
        return;
    }

    if (is_flex_element(node->style())) {
        layout_flex(node, containing_width, containing_height);
        // Apply min/max height
        auto* maxh = node->style().get("max-height");
        if (maxh && maxh->type == CSSValue::Type::LENGTH) {
            f32 mh = resolve_length(maxh->length, containing_height, font_size);
            if (node->content.height > mh) node->content.height = mh;
        }
        auto* minh = node->style().get("min-height");
        if (minh && minh->type == CSSValue::Type::LENGTH) {
            f32 mh = resolve_length(minh->length, containing_height, font_size);
            if (node->content.height < mh) node->content.height = mh;
        }
        return;
    }

    // Handle float
    auto* float_val = node->style().get("float");
    bool is_floating = float_val && float_val->type == CSSValue::Type::KEYWORD &&
                       (float_val->keyword == "left" || float_val->keyword == "right");
    if (is_floating) {
        node->is_floating = true;
        node->float_direction = float_val->keyword == "left" ? 0 : 1; // 0=left, 1=right
    }

    // Handle overflow
    auto* overflow = node->style().get("overflow");
    if (overflow && overflow->type == CSSValue::Type::KEYWORD) {
        if (overflow->keyword == "scroll" || overflow->keyword == "auto") {
            node->is_scrollable = true;
        }
    }

    layout_children(node, node->content.width, containing_height);

    auto* hv = node->style().get("height");
    if (hv && hv->type == CSSValue::Type::LENGTH) {
        node->content.height = resolve_length(hv->length, containing_height, font_size);
    } else {
        f32 max_y = 0;
        for (auto& child : node->children) {
            auto* pos = child->style().get("position");
            bool abs_pos = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
            if (abs_pos) continue;
            f32 child_bottom = child->content.y +
                               child->content.height +
                               child->padding.bottom +
                               child->border.bottom +
                               child->margin.bottom;
            if (child_bottom > max_y) max_y = child_bottom;
        }
        node->content.height = max_y;
    }

    // Apply min/max height
    auto* maxh = node->style().get("max-height");
    if (maxh && maxh->type == CSSValue::Type::LENGTH) {
        f32 mh = resolve_length(maxh->length, containing_height, font_size);
        if (node->content.height > mh) node->content.height = mh;
    }
    auto* minh = node->style().get("min-height");
    if (minh && minh->type == CSSValue::Type::LENGTH) {
        f32 mh = resolve_length(minh->length, containing_height, font_size);
        if (node->content.height < mh) node->content.height = mh;
    }

    // Apply transform
    apply_transform_to_node(node);
}

// ── layout_inline ───────────────────────────────────────────────

void LayoutEngine::layout_inline(LayoutNode* node, f32 containing_width, f32) {
    f32 char_width_factor = 0.6f;

    f32 parent_font_size = root_font_size_;
    if (node->parent) {
        auto* pfs = node->parent->style().get("font-size");
        if (pfs && pfs->type == CSSValue::Type::LENGTH && pfs->length.unit == Length::Unit::PX) {
            parent_font_size = pfs->length.value;
        }
    }
    f32 font_size = resolve_font_size(node->style(), parent_font_size);
    f32 char_width = char_width_factor * font_size;

    // line-height
    f32 line_height = font_size * 1.75f;
    auto* lh = node->style().get("line-height");
    if (lh && lh->type == CSSValue::Type::NUMBER && lh->number > 0) {
        line_height = font_size * lh->number;
    } else if (lh && lh->type == CSSValue::Type::LENGTH && lh->length.value > 0) {
        line_height = lh->length.value;
    }

    // text-align
    std::string text_align = "left";
    auto* ta = node->style().get("text-align");
    if (ta && ta->type == CSSValue::Type::KEYWORD) {
        text_align = ta->keyword;
    }

    // white-space
    std::string whitespace = "normal";
    auto* ws = node->style().get("white-space");
    if (ws && ws->type == CSSValue::Type::KEYWORD) {
        whitespace = ws->keyword;
    }

    // word-break / overflow-wrap
    auto* wb = node->style().get("word-break");
    bool break_words = (wb && wb->type == CSSValue::Type::KEYWORD && wb->keyword == "break-all");
    auto* ow = node->style().get("overflow-wrap");
    if (ow && ow->type == CSSValue::Type::KEYWORD && ow->keyword == "break-word") break_words = true;

    bool nowrap = (whitespace == "nowrap");
    bool preserve_ws = (whitespace == "pre" || whitespace == "pre-wrap" || whitespace == "pre-line");

    if (node->text().empty()) return;

    struct Word { std::string text; f32 width; };
    std::vector<Word> words;
    std::string text = node->text();
    std::size_t start = 0;

    if (preserve_ws) {
        // Pre mode: each character is a word, no collapsing
        for (char c : text) {
            f32 w = text_measure_fn_ ? text_measure_fn_(text_measurer_ctx_, std::string(1, c), (u32)font_size) : char_width;
            words.push_back({std::string(1, c), w});
        }
    } else {
        while (start < text.size()) {
            if (text[start] == ' ' && whitespace != "pre") {
                // Collapse multiple spaces
                f32 sp_w = text_measure_fn_ ? text_measure_fn_(text_measurer_ctx_, " ", (u32)font_size) : char_width;
                words.push_back({" ", sp_w});
                while (start < text.size() && text[start] == ' ') ++start;
                continue;
            }
            if (start >= text.size()) break;
            std::size_t end = text.find(' ', start);
            if (end == std::string::npos) end = text.size();
            std::string word_text = text.substr(start, end - start);
            f32 word_width = text_measure_fn_ ? text_measure_fn_(text_measurer_ctx_, word_text, (u32)font_size)
                                              : static_cast<f32>(word_text.size()) * char_width;
            words.push_back({std::move(word_text), word_width});
            start = end;
        }
    }

    // Remove trailing space if whitespace collapses
    if (whitespace == "normal" && !words.empty() && words.back().text == " ") {
        words.pop_back();
    }

    f32 line_x = 0;
    f32 line_y = 0;
    f32 max_line_width = 0;

    for (auto& word : words) {
        if (word.text == " ") {
            line_x += word.width;
            continue;
        }
        if (line_x + word.width > containing_width && line_x > 0 && !nowrap) {
            // Break long words if needed
            if (break_words && word.width > containing_width) {
                // Force-break this word at the character level
            }
            if (line_x > max_line_width) max_line_width = line_x;
            line_y += line_height;
            line_x = 0;
        }
        line_x += word.width;
        if (line_x > max_line_width) max_line_width = line_x;
    }

    if (max_line_width > 0 && !words.empty() && words.back().text == " ") {
        max_line_width -= words.back().width;
    }
    f32 total_height = line_y + (line_x > 0 ? line_height : 0);

    node->content.width = text_align == "center" ? containing_width :
                          text_align == "right" ? containing_width : max_line_width;
    node->content.height = total_height;
}

// ── layout_children ─────────────────────────────────────────────

void LayoutEngine::layout_children(LayoutNode* node, f32 containing_width, f32 containing_height) {
    if (!node || node->children.empty()) return;
    if (is_flex_element(node->style())) return;
    if (is_grid_element(node->style())) return;

    bool has_block_child = false;
    bool has_inline_child = false;

    for (auto& child : node->children) {
        auto* pos = child->style().get("position");
        bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
        if (is_absolute) continue;

        if (child->is_text()) {
            has_inline_child = true;
        } else {
            if (is_block_element(child->style())) has_block_child = true;
            else has_inline_child = true;
        }
    }

    // Track floats
    f32 float_left_x = 0;
    f32 float_right_x = containing_width;

    if (has_block_child) {
        f32 current_y = 0;
        f32 prev_margin_bottom = 0;
        bool first = true;

        for (auto& child : node->children) {
            auto* pos = child->style().get("position");
            bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
            if (is_absolute) continue;

            if (child->is_text()) {
                layout_inline(child.get(), containing_width, containing_height);
                child->content.x = 0;
                child->content.y = current_y;
                current_y += child->content.height;
                continue;
            }

            layout_block(child.get(), containing_width, containing_height);

            // Handle floats
            if (child->is_floating) {
                f32 float_margin_box_w = child->content.width + child->padding.left + child->padding.right +
                                         child->border.left + child->border.right +
                                         child->margin.left + child->margin.right;
                if (child->float_direction == 0) { // left
                    child->content.x = float_left_x;
                    float_left_x += float_margin_box_w;
                } else { // right
                    child->content.x = containing_width - float_margin_box_w - float_right_x + float_right_x;
                    child->content.x = containing_width - float_margin_box_w;
                    float_right_x -= float_margin_box_w;
                }
                child->content.y = current_y;
                current_y += child->content.height;
                continue;
            }

            // Parent-child margin collapse
            f32 collapsed_gap;
            if (first) {
                f32 parent_margin_top = node->margin.top;
                bool parent_has_border_padding_top = node->border.top > 0 || node->padding.top > 0;
                if (!parent_has_border_padding_top && parent_margin_top > 0) {
                    if (child->margin.top > parent_margin_top)
                        collapsed_gap = child->margin.top - parent_margin_top;
                    else
                        collapsed_gap = 0;
                } else {
                    collapsed_gap = child->margin.top;
                }
            } else {
                collapsed_gap = std::max(prev_margin_bottom, child->margin.top);
            }

            child->content.x = child->margin.left + child->border.left + child->padding.left;

            // Handle clear
            auto* clear_val = child->style().get("clear");
            if (clear_val && clear_val->type == CSSValue::Type::KEYWORD) {
                if (clear_val->keyword == "left" || clear_val->keyword == "both") {
                    float_left_x = 0;
                }
                if (clear_val->keyword == "right" || clear_val->keyword == "both") {
                    float_right_x = containing_width;
                }
            }

            if (child->is_text()) {
                auto* ta = node->style().get("text-align");
                if (ta && ta->type == CSSValue::Type::KEYWORD) {
                    f32 remaining = node->content.width
                                    - child->margin.left - child->border.left - child->padding.left
                                    - child->padding.right - child->border.right - child->margin.right
                                    - child->content.width;
                    if (ta->keyword == "center" && remaining > 0)
                        child->content.x += remaining / 2.0f;
                    else if (ta->keyword == "right" && remaining > 0)
                        child->content.x += remaining;
                }
            }

            child->content.y = current_y + collapsed_gap;

            f32 child_border_bottom = child->content.y +
                                      child->content.height +
                                      child->padding.bottom +
                                      child->border.bottom;
            current_y = child_border_bottom;
            prev_margin_bottom = child->margin.bottom;
            first = false;
        }
    } else if (has_inline_child) {
        f32 line_x = 0;
        f32 line_y = 0;
        f32 cur_line_height = 0;

        for (auto& child : node->children) {
            if (child->is_text()) {
                layout_inline(child.get(), containing_width, containing_height);
            } else {
                layout_block(child.get(), containing_width, containing_height);
            }

            if (line_x + child->content.width > containing_width && line_x > 0) {
                line_y += cur_line_height;
                line_x = 0;
                cur_line_height = 0;
            }
            child->content.x = line_x;
            child->content.y = line_y;
            line_x += child->content.width;
            if (child->content.height > cur_line_height)
                cur_line_height = child->content.height;
        }

        node->content.width = containing_width;
        node->content.height = line_y + cur_line_height;
    }
}

// ── Absolute / relative positioning pass ────────────────────────

void LayoutEngine::layout_absolute_pass(LayoutNode* node, LayoutNode* containing_block,
                                         f32 cb_width, f32 cb_height, f32 font_size)
{
    if (!node) return;

    auto* pos = node->style().get("position");
    bool is_relative = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "relative";

    if (is_relative) {
        f32 left_val = resolve_property(node->style(), "left", cb_width, font_size);
        f32 top_val = resolve_property(node->style(), "top", cb_height, font_size);
        f32 right_val = resolve_property(node->style(), "right", cb_width, font_size);
        f32 bottom_val = resolve_property(node->style(), "bottom", cb_height, font_size);
        if (left_val != 0) node->content.x += left_val;
        else if (right_val != 0) node->content.x -= right_val;
        if (top_val != 0) node->content.y += top_val;
        else if (bottom_val != 0) node->content.y -= bottom_val;
    }

    for (auto& child : node->children) {
        auto* child_pos = child->style().get("position");
        bool child_is_absolute = child_pos && child_pos->type == CSSValue::Type::KEYWORD &&
                                 child_pos->keyword == "absolute";
        bool child_is_fixed = child_pos && child_pos->type == CSSValue::Type::KEYWORD &&
                               child_pos->keyword == "fixed";
        bool child_is_sticky = child_pos && child_pos->type == CSSValue::Type::KEYWORD &&
                                child_pos->keyword == "sticky";

        f32 child_cb_width = cb_width;
        f32 child_cb_height = cb_height;
        f32 child_font_size = font_size;

        if (child_is_absolute || child_is_fixed) {
            LayoutNode* child_cb = nullptr;
            if (child_is_fixed) {
                // Fixed: relative to viewport
                child_cb_width = viewport_width_;
                child_cb_height = viewport_height_;
                child_font_size = root_font_size_;

                layout_block(child.get(), child_cb_width, child_cb_height);

                f32 left_off = resolve_property(child->style(), "left",
                                                child_cb_width, child_font_size);
                f32 top_off = resolve_property(child->style(), "top",
                                               child_cb_height, child_font_size);

                child->content.x = left_off;
                child->content.y = top_off;
            } else {
                child_cb = find_positioned_ancestor(child.get());
                if (child_cb) {
                    auto cb_padding = child_cb->get_padding_box();
                    child_cb_width = cb_padding.width;
                    child_cb_height = cb_padding.height;
                    child_font_size = resolve_font_size(child_cb->style(), font_size);

                    layout_block(child.get(), child_cb_width, child_cb_height);

                    f32 left_off = resolve_property(child->style(), "left",
                                                    child_cb_width, child_font_size);
                    f32 top_off = resolve_property(child->style(), "top",
                                                   child_cb_height, child_font_size);

                    child->content.x = cb_padding.x + left_off;
                    child->content.y = cb_padding.y + top_off;
                } else {
                    child_cb_width = viewport_width_;
                    child_cb_height = viewport_height_;

                    layout_block(child.get(), child_cb_width, child_cb_height);

                    f32 left_off = resolve_property(child->style(), "left",
                                                    child_cb_width, root_font_size_);
                    f32 top_off = resolve_property(child->style(), "top",
                                                   child_cb_height, root_font_size_);

                    child->content.x = left_off;
                    child->content.y = top_off;
                }
            }
        }

        if (child_is_sticky) {
            // Sticky: initially positioned as normal, then sticky offset applied
            f32 sticky_top = resolve_property(child->style(), "top",
                                               viewport_height_, font_size);
            f32 sticky_left = resolve_property(child->style(), "left",
                                                viewport_width_, font_size);

            // For now, just apply the sticky offset like relative
            if (sticky_left != 0) child->content.x += sticky_left;
            if (sticky_top != 0) child->content.y += sticky_top;
        }

        f32 child_fs = resolve_font_size(child->style(), font_size);

        if (child_is_absolute || child_is_fixed) {
            layout_absolute_pass(child.get(), nullptr, child_cb_width, child_cb_height, child_fs);
        } else {
            LayoutNode* next_cb = is_positioned(child->style()) ? child.get() : containing_block;
            layout_absolute_pass(child.get(), next_cb, child->content.width,
                                 child->content.height, child_fs);
        }
    }
}

// ── LayoutEngine::layout_async ──────────────────────────────────

LayoutEngine::LayoutEngine() = default;

async::task<std::unique_ptr<LayoutNode>> LayoutEngine::layout_async(
    html::Document* doc,
    std::unordered_map<const html::Element*, ComputedStyle>& styles,
    f32 viewport_width,
    f32 viewport_height)
{
    co_await async::thread_pool_executor{};
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;

    auto* body = html::find_element_by_tag(doc, "body");
    if (!body) {
        body = html::find_element_by_tag(doc, "html");
    }
    if (!body) co_return nullptr;

    auto tree = build_layout_tree(body, styles);
    if (!tree) co_return nullptr;

    layout_block(tree.get(), viewport_width, viewport_height);

    tree->content.x = tree->margin.left + tree->border.left + tree->padding.left;
    tree->content.y = tree->margin.top + tree->border.top + tree->padding.top;

    f32 body_font_size = resolve_font_size(tree->style(), root_font_size_);
    layout_absolute_pass(tree.get(), nullptr, viewport_width, viewport_height, body_font_size);

    co_return tree;
}

// ── Grid layout ─────────────────────────────────────────────────

struct GridState {
    std::vector<GridTrackDef> columns, rows;
    struct Cell { LayoutNode* item = nullptr; i32 col_span = 1, row_span = 1; };
    std::map<std::pair<i32,i32>, Cell> cells;
    i32 num_columns = 0, num_rows = 0;
    f32 column_gap = 0, row_gap = 0;
};

void LayoutEngine::resolve_grid_tracks(std::vector<GridTrackDef>& tracks,
                                        f32 container_size, f32 font_size, f32 gap)
{
    if (tracks.empty()) return;

    f32 total_fixed = 0;
    f32 total_fr = 0;
    f32 total_gap = gap * static_cast<f32>(static_cast<i32>(tracks.size()) - 1);

    for (auto& t : tracks) {
        switch (t.type) {
            case GridTrackType::FIXED:
                t.resolved_size = resolve_length(t.min_size, container_size, font_size);
                total_fixed += t.resolved_size;
                break;
            case GridTrackType::MINMAX: {
                f32 min_val = resolve_length(t.min_size, container_size, font_size);
                if (t.max_size.unit == Length::Unit::NONE) {
                    t.resolved_size = min_val;
                    total_fixed += t.resolved_size;
                } else {
                    f32 max_val = resolve_length(t.max_size, container_size, font_size);
                    t.resolved_size = std::max(min_val, max_val);
                    total_fixed += t.resolved_size;
                }
                break;
            }
            case GridTrackType::FIT_CONTENT: {
                f32 max_val = resolve_length(t.max_size, container_size, font_size);
                t.resolved_size = max_val;
                total_fixed += t.resolved_size;
                break;
            }
            case GridTrackType::FLEX:
                t.resolved_size = 0;
                total_fr += t.min_size.value;
                break;
            case GridTrackType::AUTO:
                t.resolved_size = 0;
                break;
        }
    }

    if (total_fr > 0) {
        f32 remaining = container_size - total_fixed - total_gap;
        if (remaining < 0) remaining = 0;
        for (auto& t : tracks) {
            if (t.type == GridTrackType::FLEX) {
                t.resolved_size = remaining * (t.min_size.value / total_fr);
            }
        }
    }
}

static std::string get_grid_prop_raw(const ComputedStyle& style, const std::string& prop) {
    auto* v = style.get(prop);
    if (!v) return "";
    if (v->type == CSSValue::Type::STRING) return v->string_value;
    if (v->type == CSSValue::Type::KEYWORD) return v->keyword;
    if (v->type == CSSValue::Type::FUNCTION) {
        if (!v->string_value.empty()) return v->string_value;
        return v->keyword;
    }
    if (v->type == CSSValue::Type::NUMBER) {
        return std::to_string(static_cast<i32>(v->number));
    }
    if (v->type == CSSValue::Type::LENGTH) {
        std::string s = std::to_string(v->length.value);
        auto dot = s.find('.');
        if (dot != std::string::npos) {
            auto last = s.find_last_not_of('0');
            if (last > dot) s = s.substr(0, last + 1);
            else if (last == dot) s = s.substr(0, dot);
        }
        switch (v->length.unit) {
            case Length::Unit::PX: s += "px"; break;
            case Length::Unit::EM: s += "em"; break;
            case Length::Unit::REM: s += "rem"; break;
            case Length::Unit::PERCENT: s += "%"; break;
            default: break;
        }
        return s;
    }
    return "";
}

void LayoutEngine::layout_grid(LayoutNode* node, f32 containing_width, f32 containing_height) {
    if (!node) return;

    f32 parent_font_size = root_font_size_;
    if (node->parent) {
        auto* pfs = node->parent->style().get("font-size");
        if (pfs && pfs->type == CSSValue::Type::LENGTH && pfs->length.unit == Length::Unit::PX) {
            parent_font_size = pfs->length.value;
        }
    }
    f32 font_size = resolve_font_size(node->style(), parent_font_size);

    EdgeSizes margins;
    margins.top = resolve_side_value(node->style(), "margin-top", "margin", containing_width, font_size);
    margins.bottom = resolve_side_value(node->style(), "margin-bottom", "margin", containing_width, font_size);
    margins.left = resolve_side_value(node->style(), "margin-left", "margin", containing_width, font_size);
    margins.right = resolve_side_value(node->style(), "margin-right", "margin", containing_width, font_size);
    node->margin = margins;

    EdgeSizes paddings;
    paddings.top = resolve_side_value(node->style(), "padding-top", "padding", containing_width, font_size);
    paddings.bottom = resolve_side_value(node->style(), "padding-bottom", "padding", containing_width, font_size);
    paddings.left = resolve_side_value(node->style(), "padding-left", "padding", containing_width, font_size);
    paddings.right = resolve_side_value(node->style(), "padding-right", "padding", containing_width, font_size);
    node->padding = paddings;

    EdgeSizes borders;
    borders.top = resolve_side_value(node->style(), "border-top-width", "border-width", containing_width, font_size);
    borders.bottom = resolve_side_value(node->style(), "border-bottom-width", "border-width", containing_width, font_size);
    borders.left = resolve_side_value(node->style(), "border-left-width", "border-width", containing_width, font_size);
    borders.right = resolve_side_value(node->style(), "border-right-width", "border-width", containing_width, font_size);
    if (borders.top == 0) {
        auto* bv = node->style().get("border");
        if (bv && bv->type == CSSValue::Type::LENGTH) {
            f32 bw = resolve_length(bv->length, containing_width, font_size);
            borders = {bw, bw, bw, bw};
        }
    }
    node->border = borders;

    f32 h_padding = paddings.left + paddings.right;
    f32 h_border = borders.left + borders.right;
    f32 h_margin = margins.left + margins.right;

    f32 container_width = containing_width - h_margin - h_padding - h_border;
    if (container_width < 0) container_width = 0;

    auto* wv = node->style().get("width");
    if (wv && wv->type == CSSValue::Type::LENGTH) {
        container_width = resolve_length(wv->length, containing_width, font_size);
    }

    f32 container_height = containing_height;
    auto* hv = node->style().get("height");
    if (hv && hv->type == CSSValue::Type::LENGTH) {
        container_height = resolve_length(hv->length, containing_height, font_size);
    }

    node->content.width = container_width;

    // Parse grid template
    std::string cols_str = get_grid_prop_raw(node->style(), "grid-template-columns");
    std::string rows_str = get_grid_prop_raw(node->style(), "grid-template-rows");

    GridState state;
    state.columns = parse_track_list(cols_str, container_width, font_size);
    state.rows = parse_track_list(rows_str, container_height, font_size);

    if (state.columns.empty()) {
        state.columns.push_back(GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
    }

    auto* gap_v = node->style().get("gap");
    if (gap_v && gap_v->type == CSSValue::Type::LENGTH) {
        f32 g = resolve_length(gap_v->length, container_width, font_size);
        state.column_gap = g;
        state.row_gap = g;
    }
    auto* cg = node->style().get("column-gap");
    if (cg && cg->type == CSSValue::Type::LENGTH) {
        state.column_gap = resolve_length(cg->length, container_width, font_size);
    }
    auto* rg = node->style().get("row-gap");
    if (rg && rg->type == CSSValue::Type::LENGTH) {
        state.row_gap = resolve_length(rg->length, container_width, font_size);
    }

    state.num_columns = static_cast<i32>(state.columns.size());
    state.num_rows = static_cast<i32>(state.rows.size());

    // Place items
    std::vector<LayoutNode*> unplaced;
    for (auto& child : node->children) {
        auto* pos = child->style().get("position");
        bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
        if (is_absolute) continue;

        std::string gc_str = get_grid_prop_raw(child->style(), "grid-column");
        std::string gr_str = get_grid_prop_raw(child->style(), "grid-row");

        GridPlacement col_place = gc_str.empty() ? GridPlacement{} : parse_grid_line(gc_str);
        GridPlacement row_place = gr_str.empty() ? GridPlacement{} : parse_grid_line(gr_str);

        bool has_explicit = (col_place.line_start > 0 || row_place.line_start > 0);

        if (has_explicit) {
            i32 col = col_place.line_start > 0 ? col_place.line_start - 1 : 0;
            i32 row = row_place.line_start > 0 ? row_place.line_start - 1 : 0;
            i32 col_span = static_cast<i32>(col_place.span);
            i32 row_span = static_cast<i32>(row_place.span);

            while (col + col_span > state.num_columns) {
                state.columns.push_back(GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                state.num_columns = static_cast<i32>(state.columns.size());
            }
            while (row + row_span > state.num_rows) {
                state.rows.push_back(GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                state.num_rows = static_cast<i32>(state.rows.size());
            }

            for (i32 c = 0; c < col_span; c++) {
                for (i32 r = 0; r < row_span; r++) {
                    state.cells[{col + c, row + r}] = GridState::Cell{child.get(), col_span, row_span};
                }
            }
        } else {
            unplaced.push_back(child.get());
        }
    }

    // Auto-place remaining items
    if (!unplaced.empty()) {
        i32 cursor_col = 0;
        i32 cursor_row = 0;
        for (auto* item : unplaced) {
            std::string gc_str = get_grid_prop_raw(item->style(), "grid-column");
            std::string gr_str = get_grid_prop_raw(item->style(), "grid-row");
            GridPlacement cp = gc_str.empty() ? GridPlacement{} : parse_grid_line(gc_str);
            GridPlacement rp = gr_str.empty() ? GridPlacement{} : parse_grid_line(gr_str);

            i32 col_span = static_cast<i32>(cp.span);
            i32 row_span = static_cast<i32>(rp.span);

            while (cursor_col + col_span > state.num_columns) {
                state.columns.push_back(GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                state.num_columns = static_cast<i32>(state.columns.size());
            }

            bool placed = false;
            i32 safety = 0;
            while (!placed && safety < 10000) {
                safety++;
                bool fits = true;
                for (i32 dc = 0; dc < col_span && fits; dc++) {
                    for (i32 dr = 0; dr < row_span && fits; dr++) {
                        i32 check_col = cursor_col + dc;
                        i32 check_row = cursor_row + dr;
                        if (check_col >= state.num_columns) { fits = false; }
                        auto it = state.cells.find({check_col, check_row});
                        if (it != state.cells.end()) { fits = false; }
                    }
                }
                if (fits) {
                    for (i32 dc = 0; dc < col_span; dc++) {
                        for (i32 dr = 0; dr < row_span; dr++) {
                            state.cells[{cursor_col + dc, cursor_row + dr}] = GridState::Cell{item, col_span, row_span};
                        }
                    }
                    placed = true;
                } else {
                    cursor_col++;
                    if (cursor_col + col_span > state.num_columns) {
                        cursor_col = 0;
                        cursor_row++;
                    }
                }
            }
            while (cursor_row + row_span > state.num_rows) {
                state.rows.push_back(GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                state.num_rows = static_cast<i32>(state.rows.size());
            }
            cursor_col += col_span;
            if (cursor_col >= state.num_columns) {
                cursor_col = 0;
                cursor_row++;
            }
        }
    }

    // Resolve track sizes
    resolve_grid_tracks(state.columns, container_width, font_size, state.column_gap);
    resolve_grid_tracks(state.rows, container_height, font_size, state.row_gap);

    // Position and size items
    auto cell_start = [](const std::vector<GridTrackDef>& tracks, i32 index, f32 gap) -> f32 {
        f32 pos = 0;
        for (i32 i = 0; i < index; i++) {
            if (i > 0) pos += gap;
            pos += tracks[i].resolved_size;
        }
        return pos;
    };

    auto cell_span_size = [](const std::vector<GridTrackDef>& tracks, i32 from, i32 count, f32 gap) -> f32 {
        f32 size = 0;
        for (i32 i = from; i < from + count; i++) {
            if (i > from) size += gap;
            size += tracks[i].resolved_size;
        }
        return size;
    };

    struct GridAlign { JustifyContent justify; AlignItems align; bool stretch_w; bool stretch_h; };

    auto get_grid_align = [](const ComputedStyle& s) -> GridAlign {
        GridAlign ga{JustifyContent::FLEX_START, AlignItems::STRETCH, true, true};
        auto* js = s.get("justify-self");
        if (js && js->type == CSSValue::Type::KEYWORD) {
            if (js->keyword == "start" || js->keyword == "flex-start") { ga.justify = JustifyContent::FLEX_START; ga.stretch_w = false; }
            else if (js->keyword == "end" || js->keyword == "flex-end") { ga.justify = JustifyContent::FLEX_END; ga.stretch_w = false; }
            else if (js->keyword == "center") { ga.justify = JustifyContent::CENTER; ga.stretch_w = false; }
            else if (js->keyword == "stretch") { ga.justify = JustifyContent::FLEX_START; ga.stretch_w = true; }
        }
        auto* as = s.get("align-self");
        if (as && as->type == CSSValue::Type::KEYWORD) {
            if (as->keyword == "start" || as->keyword == "flex-start") { ga.align = AlignItems::FLEX_START; ga.stretch_h = false; }
            else if (as->keyword == "end" || as->keyword == "flex-end") { ga.align = AlignItems::FLEX_END; ga.stretch_h = false; }
            else if (as->keyword == "center") { ga.align = AlignItems::CENTER; ga.stretch_h = false; }
            else if (as->keyword == "stretch") { ga.align = AlignItems::STRETCH; ga.stretch_h = true; }
        }
        return ga;
    };

    std::unordered_set<LayoutNode*> sized_items;
    for (auto& [coord, cell] : state.cells) {
        if (sized_items.count(cell.item)) continue;
        sized_items.insert(cell.item);

        auto [col, row] = coord;
        LayoutNode* item = cell.item;

        f32 cell_x = cell_start(state.columns, col, state.column_gap);
        f32 cell_y = cell_start(state.rows, row, state.row_gap);
        f32 cell_w = cell_span_size(state.columns, col, cell.col_span, state.column_gap);
        f32 cell_h = cell_span_size(state.rows, row, cell.row_span, state.row_gap);

        EdgeSizes item_margins;
        item_margins.left = resolve_side_value(item->style(), "margin-left", "margin", cell_w, font_size);
        item_margins.right = resolve_side_value(item->style(), "margin-right", "margin", cell_w, font_size);
        item_margins.top = resolve_side_value(item->style(), "margin-top", "margin", cell_h, font_size);
        item_margins.bottom = resolve_side_value(item->style(), "margin-bottom", "margin", cell_h, font_size);
        item->margin = item_margins;

        EdgeSizes item_borders;
        item_borders.left = resolve_side_value(item->style(), "border-left-width", "border-width", cell_w, font_size);
        item_borders.right = resolve_side_value(item->style(), "border-right-width", "border-width", cell_w, font_size);
        item_borders.top = resolve_side_value(item->style(), "border-top-width", "border-width", cell_h, font_size);
        item_borders.bottom = resolve_side_value(item->style(), "border-bottom-width", "border-width", cell_h, font_size);
        if (item_borders.top == 0) {
            auto* bv = item->style().get("border");
            if (bv && bv->type == CSSValue::Type::LENGTH) {
                f32 bw = resolve_length(bv->length, cell_w, font_size);
                item_borders = {bw, bw, bw, bw};
            }
        }
        item->border = item_borders;

        EdgeSizes item_paddings;
        item_paddings.left = resolve_side_value(item->style(), "padding-left", "padding", cell_w, font_size);
        item_paddings.right = resolve_side_value(item->style(), "padding-right", "padding", cell_w, font_size);
        item_paddings.top = resolve_side_value(item->style(), "padding-top", "padding", cell_h, font_size);
        item_paddings.bottom = resolve_side_value(item->style(), "padding-bottom", "padding", cell_h, font_size);
        item->padding = item_paddings;

        GridAlign ga = get_grid_align(item->style());

        f32 content_w = cell_w - item_margins.left - item_margins.right
                        - item_borders.left - item_borders.right
                        - item_paddings.left - item_paddings.right;
        if (content_w < 0) content_w = 0;

        f32 content_h = cell_h - item_margins.top - item_margins.bottom
                        - item_borders.top - item_borders.bottom
                        - item_paddings.top - item_paddings.bottom;
        if (content_h < 0) content_h = 0;

        if (!ga.stretch_h) content_h = 0;
        if (!ga.stretch_w) content_w = 0;

        item->content.width = content_w;
        item->content.height = content_h;

        item->content.x = cell_x + item_margins.left + item_borders.left + item_paddings.left;
        item->content.y = cell_y + item_margins.top + item_borders.top + item_paddings.top;

        // Layout children
        if (!item->children.empty()) {
            if (is_flex_element(item->style())) {
                f32 saved_x = item->content.x;
                f32 saved_y = item->content.y;
                layout_flex(item, content_w, content_h);
                item->content.x = saved_x;
                item->content.y = saved_y;
                item->content.width = content_w;
                item->content.height = content_h;
            } else if (is_grid_element(item->style())) {
                f32 saved_x = item->content.x;
                f32 saved_y = item->content.y;
                layout_grid(item, content_w, content_h);
                item->content.x = saved_x;
                item->content.y = saved_y;
                item->content.width = content_w;
                item->content.height = content_h;
            } else {
                layout_children(item, content_w, content_h);
            }
        }

        // Alignment
        {
            f32 extra_w = cell_w - item->content.width
                          - item_margins.left - item_margins.right
                          - item_borders.left - item_borders.right
                          - item_paddings.left - item_paddings.right;
            if (extra_w > 0) {
                if (ga.justify == JustifyContent::CENTER) item->content.x += extra_w / 2;
                else if (ga.justify == JustifyContent::FLEX_END) item->content.x += extra_w;
            }
        }
        {
            f32 extra_h = cell_h - item->content.height
                          - item_margins.top - item_margins.bottom
                          - item_borders.top - item_borders.bottom
                          - item_paddings.top - item_paddings.bottom;
            if (extra_h > 0) {
                if (ga.align == AlignItems::CENTER) item->content.y += extra_h / 2;
                else if (ga.align == AlignItems::FLEX_END) item->content.y += extra_h;
            }
        }
    }

    f32 total_col_size = cell_start(state.columns, state.num_columns, state.column_gap);
    f32 total_row_size = cell_start(state.rows, state.num_rows, state.row_gap);

    node->content.width = total_col_size;
    node->content.height = total_row_size;

    if (wv && wv->type == CSSValue::Type::LENGTH) {
        node->content.width = resolve_length(wv->length, containing_width, font_size);
    }
    if (hv && hv->type == CSSValue::Type::LENGTH) {
        node->content.height = resolve_length(hv->length, containing_height, font_size);
    }
}

}
