#include "hit_test.hpp"

#include "../css/layout.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace browser::html {

    namespace {

        bool point_in_rect(f32 px, f32 py, const css::Rect &r) {
            return px >= r.x && px <= r.x + r.width && py >= r.y && py <= r.y + r.height;
        }

        bool point_in_border_radius(f32 px, f32 py, const css::Rect &r, f32 radius) {
            if (radius <= 0)
                return true;
            // Point must be inside the rect first
            if (px < r.x || px > r.x + r.width || py < r.y || py > r.y + r.height)
                return false;
            // For each corner, if point is in that corner's radius quadrant,
            // check distance from the arc center
            // Top-left
            if (px < r.x + radius && py < r.y + radius) {
                f32 dx = px - (r.x + radius);
                f32 dy = py - (r.y + radius);
                return (dx * dx + dy * dy) <= radius * radius;
            }
            // Top-right
            if (px > r.x + r.width - radius && py < r.y + radius) {
                f32 dx = px - (r.x + r.width - radius);
                f32 dy = py - (r.y + radius);
                return (dx * dx + dy * dy) <= radius * radius;
            }
            // Bottom-left
            if (px < r.x + radius && py > r.y + r.height - radius) {
                f32 dx = px - (r.x + radius);
                f32 dy = py - (r.y + r.height - radius);
                return (dx * dx + dy * dy) <= radius * radius;
            }
            // Bottom-right
            if (px > r.x + r.width - radius && py > r.y + r.height - radius) {
                f32 dx = px - (r.x + r.width - radius);
                f32 dy = py - (r.y + r.height - radius);
                return (dx * dx + dy * dy) <= radius * radius;
            }
            return true;
        }

        bool is_positioned(const css::ComputedStyle &style) {
            auto *pos = style.get("position");
            if (!pos || pos->type != css::CSSValue::Type::KEYWORD)
                return false;
            return pos->keyword == "relative" || pos->keyword == "absolute" || pos->keyword == "fixed" ||
                   pos->keyword == "sticky";
        }

        f32 get_z(const css::ComputedStyle &style) {
            auto *z = style.get("z-index");
            if (!z || z->type != css::CSSValue::Type::NUMBER)
                return 0;
            return z->number;
        }

        bool is_pointer_events_none(const css::ComputedStyle &style) {
            auto *pe = style.get("pointer-events");
            return pe && pe->type == css::CSSValue::Type::KEYWORD && pe->keyword == "none";
        }

        bool is_visibility_hidden(const css::ComputedStyle &style) {
            auto *v = style.get("visibility");
            return v && v->type == css::CSSValue::Type::KEYWORD && v->keyword == "hidden";
        }

        struct HitCandidate {
            const css::LayoutNode *node = nullptr;
            css::Rect box;
            f32 z_index = 0;
            int paint_order = 0;  // lower = painted earlier (further back)
            bool is_positioned_flag = false;
        };

        void collect_candidates(const css::LayoutNode *node,
                                f32 ox,
                                f32 oy,
                                f32 px,
                                f32 py,
                                std::vector<HitCandidate> &candidates,
                                int &depth) {
            if (!node)
                return;

            auto &style = node->style();

            if (is_pointer_events_none(style))
                return;

            css::Rect border_box = node->get_border_box();
            border_box.x += ox;
            border_box.y += oy;

            css::Rect padding_box = node->get_padding_box();
            padding_box.x += ox;
            padding_box.y += oy;

            // Check overflow hidden clip (clips to padding edge, like painter.cpp does)
            bool has_clip = false;
            css::Rect clip_rect = padding_box;
            {
                auto *overflow = style.get("overflow");
                if (overflow && overflow->type == css::CSSValue::Type::KEYWORD) {
                    has_clip =
                        (overflow->keyword == "hidden" || overflow->keyword == "scroll" || overflow->keyword == "auto");
                }
                if (!has_clip) {
                    auto *oxv = style.get("overflow-x");
                    if (oxv && oxv->type == css::CSSValue::Type::KEYWORD) {
                        has_clip = (oxv->keyword == "hidden" || oxv->keyword == "scroll" || oxv->keyword == "auto");
                    }
                }
                if (!has_clip) {
                    auto *oyv = style.get("overflow-y");
                    if (oyv && oyv->type == css::CSSValue::Type::KEYWORD) {
                        has_clip = (oyv->keyword == "hidden" || oyv->keyword == "scroll" || oyv->keyword == "auto");
                    }
                }
            }

            // Check if point is in border box
            bool in_box = point_in_rect(px, py, border_box);

            // Even if not in box, children might be (for things like negative margins)
            // But for hit testing, we only add this node if the point is in its bounds
            if (in_box && !node->is_text()) {
                // Check border-radius clip
                auto *br = style.get("border-radius");
                f32 radius = 0;
                if (br && br->type == css::CSSValue::Type::LENGTH) {
                    radius = br->length.value;
                }
                bool passes_radius = point_in_border_radius(px, py, border_box, radius);

                if (passes_radius && !is_visibility_hidden(style)) {
                    f32 zi = get_z(style);
                    bool pos = is_positioned(style);
                    int order = depth;
                    // Elements without position and z-index=0 paint in tree order
                    // Positioned/stacking contexts with z-index auto = 0
                    // Adjust paint_order to reflect paint layer ordering
                    candidates.push_back({node, border_box, zi, order, pos});
                }
            }

            // Recurse into children
            f32 child_ox = ox + node->content.x + node->scroll_offset_x;
            f32 child_oy = oy + node->content.y + node->scroll_offset_y;

            // Collect children in paint order: negative z → normal flow → positive z
            std::vector<const css::LayoutNode *> negative_z;
            std::vector<const css::LayoutNode *> normal_flow;
            std::vector<const css::LayoutNode *> positive_z;

            for (auto &child : node->children) {
                auto &cs = child->style();
                f32 zi = css::LayoutEngine::get_z_index(cs);
                bool creates_stack = css::LayoutEngine::creates_stacking_context(cs);

                if (creates_stack) {
                    if (zi < 0)
                        negative_z.push_back(child.get());
                    else
                        positive_z.push_back(child.get());
                } else {
                    normal_flow.push_back(child.get());
                }
            }

            depth++;

            // If has_clip and point is outside clip rect, don't recurse into children
            bool pass_clip = !has_clip || point_in_rect(px, py, clip_rect);

            if (pass_clip) {
                for (auto *child : negative_z) collect_candidates(child, child_ox, child_oy, px, py, candidates, depth);
                for (auto *child : normal_flow)
                    collect_candidates(child, child_ox, child_oy, px, py, candidates, depth);
                for (auto *child : positive_z) collect_candidates(child, child_ox, child_oy, px, py, candidates, depth);
            }

            depth--;
        }

    }  // namespace

    HitTestResult hit_test(const css::LayoutNode *layout_root, f32 px, f32 py) {
        HitTestResult result;

        if (!layout_root)
            return result;

        std::vector<HitCandidate> candidates;
        int depth = 0;
        collect_candidates(layout_root, 0, 0, px, py, candidates, depth);

        if (candidates.empty())
            return result;

        // Sort by paint order: backgrounds → negative z-index → non-positioned → positive z-index
        // Negative z-index paints first (furthest back), then non-positioned (tree order),
        // then positive z-index (last = topmost)
        std::stable_sort(candidates.begin(), candidates.end(), [](const HitCandidate &a, const HitCandidate &b) {
            // Both have z-index: sort by z then paint order
            if (a.z_index != b.z_index)
                return a.z_index < b.z_index;
            return a.paint_order < b.paint_order;
        });

        // Last element after sorting is topmost
        const auto &winner = candidates.back();
        auto *n = winner.node->node();
        if (n && n->type == NodeType::ELEMENT) {
            result.element = static_cast<Element *>(n);
        }
        result.bounding_box = winner.box;
        result.hit_rect = winner.box;
        result.z_index = winner.z_index;
        result.is_scrollable = winner.node->is_scrollable;

        return result;
    }

}  // namespace browser::html
