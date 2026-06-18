#pragma once
#include "../async/task.hpp"
#include "../html/dom.hpp"
#include "cascade.hpp"
#include "css_values.hpp"
#include "layout/types.hpp"

namespace browser::css {

    struct LineInfo {
        std::string text;  // rendered text for this line
        f32 y;             // y-position from top of node content
    };

    class LayoutNode {
    public:
        LayoutNode(html::Element *element, ComputedStyle style);
        LayoutNode(const std::string &text, ComputedStyle style);
        LayoutNode(const LayoutNode &) = delete;
        LayoutNode &operator=(const LayoutNode &) = delete;
        virtual ~LayoutNode() = default;

        std::vector<std::unique_ptr<LayoutNode>> children;
        LayoutNode *parent = nullptr;

        Rect content;
        EdgeSizes padding, border, margin;

        // Extended properties
        Mat3x3 transform_matrix;
        bool has_transform = false;
        bool is_floating = false;
        u32 float_direction = 0;  // 0=left, 1=right
        bool is_scrollable = false;
        f32 scroll_offset_x = 0, scroll_offset_y = 0;
        f32 opacity = 1.0f;

        // Per-line break info for wrapped text (populated by layout_inline)
        std::vector<LineInfo> text_lines;

        Rect get_padding_box() const;
        Rect get_border_box() const;
        Rect get_margin_box() const;

        virtual void layout(f32 available_width, f32 available_height);
        void set_position(f32 x, f32 y);

        html::Node *node() const { return node_; }
        ComputedStyle &style() { return style_; }
        const ComputedStyle &style() const { return style_; }
        bool is_text() const { return is_text_; }
        const std::string &text() const { return text_; }
        const std::string &element_key() const { return element_key_; }

    private:
        html::Node *node_;
        ComputedStyle style_;
        bool is_text_ = false;
        std::string text_;
        std::string element_key_;
    };

    void apply_transform_to_node(LayoutNode *node);

    class LayoutEngine {
    public:
        LayoutEngine();
        void set_text_measure(void *ctx, TextMeasureFn fn) {
            text_measurer_ctx_ = ctx;
            text_measure_fn_ = fn;
        }
        async::task<std::unique_ptr<LayoutNode>> layout_async(
            html::Document *doc,
            const std::unordered_map<const html::Element *, ComputedStyle> &styles,
            f32 viewport_width,
            f32 viewport_height);

        static bool is_grid_element(const ComputedStyle &style);
        static f32 get_z_index(const ComputedStyle &style);
        static bool creates_stacking_context(const ComputedStyle &style);
        f32 resolve_calc_string(const std::string &calc_expr, f32 parent_value, f32 font_size) const;
        f32 resolve_clamp_func(const std::string &expr, f32 parent_value, f32 font_size) const;
        f32 resolve_func_length(const ComputedStyle &style, const CSSValue *v, f32 parent_value, f32 font_size) const;
        void resolve_grid_tracks(std::vector<GridTrackDef> &tracks, f32 container_size, f32 font_size, f32 gap = 0);

    private:
        f32 root_font_size_ = 16.0f;
        f32 viewport_width_ = 0;
        f32 viewport_height_ = 0;
        void *text_measurer_ctx_ = nullptr;
        TextMeasureFn text_measure_fn_ = nullptr;

        f32 resolve_length(const Length &len, f32 parent_value, f32 font_size) const;
        f32 resolve_font_size(const ComputedStyle &style, f32 parent_font_size) const;

        f32 resolve_side_value(const ComputedStyle &style,
                               const std::string &side_prop,
                               const std::string &shorthand_prop,
                               f32 containing,
                               f32 font_size) const;
        f32 resolve_property(const ComputedStyle &style, const std::string &prop, f32 containing, f32 font_size) const;

        void layout_block(LayoutNode *node, f32 containing_width, f32 containing_height);
        void layout_inline(LayoutNode *node, f32 containing_width, f32 containing_height);
        void layout_children(LayoutNode *node, f32 containing_width, f32 containing_height);
        void layout_absolute_pass(
            LayoutNode *node, LayoutNode *containing_block, f32 cb_width, f32 cb_height, f32 font_size);

        std::unique_ptr<LayoutNode> build_layout_tree(
            html::Node *node, const std::unordered_map<const html::Element *, ComputedStyle> &styles);

        static std::unique_ptr<LayoutNode> make_anonymous_block(ComputedStyle style);
        static bool is_block_element(const ComputedStyle &style);
        static bool is_inline_element(const ComputedStyle &style);
        static bool is_positioned(const ComputedStyle &style);
        static bool is_fixed_or_sticky(const ComputedStyle &style);
        static bool is_flex_element(const ComputedStyle &style);
        static bool is_table_element(const ComputedStyle &style);
        static LayoutNode *find_positioned_ancestor(LayoutNode *node);

        FlexConfig resolve_flex_config(const ComputedStyle &style, f32 font_size) const;
        FlexItem resolve_flex_item(LayoutNode *child,
                                   const FlexConfig &config,
                                   f32 containing_main,
                                   f32 containing_cross,
                                   f32 font_size) const;
        std::vector<FlexLine> distribute_lines(std::vector<FlexItem> &items,
                                               const FlexConfig &config,
                                               f32 container_main) const;
        void distribute_free_space(FlexLine &line, f32 container_main, const FlexConfig &config) const;
        void compute_cross_sizes(FlexLine &line, f32 container_cross, const FlexConfig &config, f32 font_size);
        void align_lines(std::vector<FlexLine> &lines, f32 container_cross, const FlexConfig &config) const;
        void layout_flex(LayoutNode *node, f32 containing_width, f32 containing_height);
        void layout_grid(LayoutNode *node, f32 containing_width, f32 containing_height);
        void layout_table(LayoutNode *node, f32 containing_width, f32 containing_height);
    };

}  // namespace browser::css
