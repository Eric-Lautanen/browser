#include "../layout.hpp"

namespace browser::css {

    bool LayoutEngine::is_block_element(const ComputedStyle &style) {
        auto *v = style.get("display");
        if (!v || v->type != CSSValue::Type::KEYWORD)
            return true;
        return v->keyword == "block" || v->keyword == "flex" || v->keyword == "grid" || v->keyword == "list-item" ||
               v->keyword == "table" || v->keyword == "table-row" || v->keyword == "table-row-group" ||
               v->keyword == "table-header-group" || v->keyword == "table-footer-group" ||
               v->keyword == "table-caption" || v->keyword == "inline-table";
    }

    bool LayoutEngine::is_inline_element(const ComputedStyle &style) {
        auto *v = style.get("display");
        if (!v || v->type != CSSValue::Type::KEYWORD)
            return false;
        return v->keyword == "inline" || v->keyword == "inline-block";
    }

    bool LayoutEngine::is_positioned(const ComputedStyle &style) {
        auto *v = style.get("position");
        if (!v || v->type != CSSValue::Type::KEYWORD)
            return false;
        return v->keyword != "static";
    }

    bool LayoutEngine::is_fixed_or_sticky(const ComputedStyle &style) {
        auto *v = style.get("position");
        if (!v || v->type != CSSValue::Type::KEYWORD)
            return false;
        return v->keyword == "fixed" || v->keyword == "sticky";
    }

    f32 LayoutEngine::get_z_index(const ComputedStyle &style) {
        auto *v = style.get("z-index");
        if (v && v->type == CSSValue::Type::NUMBER)
            return v->number;
        if (v && v->type == CSSValue::Type::KEYWORD && v->keyword == "auto")
            return 0;
        return 0;
    }

    bool LayoutEngine::creates_stacking_context(const ComputedStyle &style) {
        auto *zi = style.get("z-index");
        if (zi && zi->type == CSSValue::Type::NUMBER)
            return true;

        auto *op = style.get("opacity");
        if (op && op->type == CSSValue::Type::NUMBER && op->number < 1.0f)
            return true;

        auto *tr = style.get("transform");
        if (tr && tr->type == CSSValue::Type::TRANSFORM && !tr->transforms.empty())
            return true;

        auto *pos = style.get("position");
        if (pos && pos->type == CSSValue::Type::KEYWORD && (pos->keyword == "fixed" || pos->keyword == "sticky"))
            return true;

        return false;
    }

    LayoutNode *LayoutEngine::find_positioned_ancestor(LayoutNode *node) {
        LayoutNode *ancestor = node->parent;
        while (ancestor) {
            if (is_positioned(ancestor->style()))
                return ancestor;
            ancestor = ancestor->parent;
        }
        return nullptr;
    }

    bool LayoutEngine::is_flex_element(const ComputedStyle &style) {
        auto *v = style.get("display");
        if (!v || v->type != CSSValue::Type::KEYWORD)
            return false;
        return v->keyword == "flex" || v->keyword == "inline-flex";
    }

    bool LayoutEngine::is_grid_element(const ComputedStyle &style) {
        auto *v = style.get("display");
        if (!v || v->type != CSSValue::Type::KEYWORD)
            return false;
        return v->keyword == "grid" || v->keyword == "inline-grid";
    }

    bool LayoutEngine::is_table_element(const ComputedStyle &style) {
        auto *v = style.get("display");
        if (!v || v->type != CSSValue::Type::KEYWORD)
            return false;
        return v->keyword == "table" || v->keyword == "inline-table" || v->keyword == "table-row" ||
               v->keyword == "table-cell" || v->keyword == "table-row-group" || v->keyword == "table-header-group" ||
               v->keyword == "table-footer-group" || v->keyword == "table-column" ||
               v->keyword == "table-column-group" || v->keyword == "table-caption";
    }

}  // namespace browser::css
