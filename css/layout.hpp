#pragma once
#include <optional>
#include <vector>
#include <memory>
#include <unordered_map>
#include "../tests/utility.hpp"
#include "css_values.hpp"
#include "cascade.hpp"
#include "../html/dom.hpp"
#include "../async/task.hpp"

namespace browser::css {

struct GridTrackDef;

struct EdgeSizes { f32 top=0, right=0, bottom=0, left=0; };

using TextMeasureFn = f32 (*)(void* ctx, const std::string& text, u32 pixel_size);

struct Rect {
    f32 x=0, y=0, width=0, height=0;
    std::optional<Rect> intersect(const Rect& o) const;
};

class LayoutNode;

enum class FlexDirection { ROW, ROW_REVERSE, COLUMN, COLUMN_REVERSE };
enum class FlexWrap { NOWRAP, WRAP, WRAP_REVERSE };
enum class JustifyContent { FLEX_START, FLEX_END, CENTER, SPACE_BETWEEN, SPACE_AROUND, SPACE_EVENLY };
enum class AlignItems { STRETCH, FLEX_START, FLEX_END, CENTER, BASELINE };
enum class AlignSelf { AUTO, STRETCH, FLEX_START, FLEX_END, CENTER, BASELINE };
enum class AlignContent { FLEX_START, FLEX_END, CENTER, SPACE_BETWEEN, SPACE_AROUND, STRETCH };

struct FlexConfig {
    FlexDirection direction = FlexDirection::ROW;
    FlexWrap wrap = FlexWrap::NOWRAP;
    JustifyContent justify_content = JustifyContent::FLEX_START;
    AlignItems align_items = AlignItems::STRETCH;
    AlignContent align_content = AlignContent::STRETCH;
    f32 row_gap = 0;
    f32 column_gap = 0;
};

struct FlexItem {
    LayoutNode* node = nullptr;
    f32 base_size = 0;
    f32 hypothetical_main = 0;
    f32 target_main = 0;
    f32 cross_size = 0;
    f32 cross_offset = 0;
    f32 flex_grow = 0;
    f32 flex_shrink = 1;
    f32 flex_basis = -1;
    AlignSelf align_self = AlignSelf::AUTO;
    f32 main_margin_start = 0;
    f32 main_margin_end = 0;
    f32 cross_margin_start = 0;
    f32 cross_margin_end = 0;
    f32 main_border_padding = 0;
    f32 cross_border_padding = 0;
    bool frozen = false;
};

struct FlexLine {
    std::vector<FlexItem*> items;
    f32 main_size = 0;
    f32 cross_size = 0;
};

class LayoutNode {
public:
    LayoutNode(html::Element* element, ComputedStyle style);
    LayoutNode(const std::string& text, ComputedStyle style);
    LayoutNode(const LayoutNode&) = delete;
    LayoutNode& operator=(const LayoutNode&) = delete;
    virtual ~LayoutNode() = default;

    std::vector<std::unique_ptr<LayoutNode>> children;
    LayoutNode* parent = nullptr;

    Rect content;
    EdgeSizes padding, border, margin;

    Rect get_padding_box() const;
    Rect get_border_box() const;
    Rect get_margin_box() const;

    virtual void layout(f32 available_width, f32 available_height);
    void set_position(f32 x, f32 y);

    html::Node* node() const { return node_; }
    ComputedStyle& style() { return style_; }
    bool is_text() const { return is_text_; }
    const std::string& text() const { return text_; }

private:
    html::Node* node_;
    ComputedStyle style_;
    bool is_text_ = false;
    std::string text_;
};

class LayoutEngine {
public:
    LayoutEngine();
    void set_text_measure(void* ctx, TextMeasureFn fn) { text_measurer_ctx_ = ctx; text_measure_fn_ = fn; }
    async::task<std::unique_ptr<LayoutNode>> layout(
        html::Document* doc,
        std::unordered_map<const html::Element*, ComputedStyle>& styles,
        f32 viewport_width,
        f32 viewport_height);

    static bool is_grid_element(const ComputedStyle& style);
    void resolve_grid_tracks(std::vector<GridTrackDef>& tracks, f32 container_size, f32 font_size, f32 gap = 0);

private:
    f32 root_font_size_ = 16.0f;
    f32 viewport_width_ = 0;
    f32 viewport_height_ = 0;
    void* text_measurer_ctx_ = nullptr;
    TextMeasureFn text_measure_fn_ = nullptr;

    f32 resolve_length(const Length& len, f32 parent_value, f32 font_size) const;
    f32 resolve_font_size(const ComputedStyle& style, f32 parent_font_size) const;

    f32 resolve_side_value(const ComputedStyle& style, const std::string& side_prop,
                            const std::string& shorthand_prop, f32 containing,
                            f32 font_size) const;
    f32 resolve_property(const ComputedStyle& style, const std::string& prop,
                          f32 containing, f32 font_size) const;

    void layout_block(LayoutNode* node, f32 containing_width, f32 containing_height);
    void layout_inline(LayoutNode* node, f32 containing_width, f32 containing_height);
    void layout_children(LayoutNode* node, f32 containing_width, f32 containing_height);
    void layout_absolute_pass(LayoutNode* node, LayoutNode* containing_block,
                              f32 cb_width, f32 cb_height, f32 font_size);

    std::unique_ptr<LayoutNode> build_layout_tree(
        html::Node* node,
        std::unordered_map<const html::Element*, ComputedStyle>& styles);

    static std::unique_ptr<LayoutNode> make_anonymous_block(ComputedStyle style);
    static bool is_block_element(const ComputedStyle& style);
    static bool is_inline_element(const ComputedStyle& style);
    static bool is_positioned(const ComputedStyle& style);
    static bool is_flex_element(const ComputedStyle& style);
    static LayoutNode* find_positioned_ancestor(LayoutNode* node);

    FlexConfig resolve_flex_config(const ComputedStyle& style, f32 font_size) const;
    FlexItem resolve_flex_item(LayoutNode* child, const FlexConfig& config,
                                f32 containing_main, f32 containing_cross,
                                f32 font_size) const;
    std::vector<FlexLine> distribute_lines(std::vector<FlexItem>& items,
                                            const FlexConfig& config,
                                            f32 container_main) const;
    void distribute_free_space(FlexLine& line, f32 container_main,
                                const FlexConfig& config) const;
    void compute_cross_sizes(FlexLine& line, f32 container_cross,
                              const FlexConfig& config, f32 font_size);
    void align_lines(std::vector<FlexLine>& lines, f32 container_cross,
                      const FlexConfig& config) const;
    void layout_flex(LayoutNode* node, f32 containing_width, f32 containing_height);
    void layout_grid(LayoutNode* node, f32 containing_width, f32 containing_height);
};

} // namespace browser::css
