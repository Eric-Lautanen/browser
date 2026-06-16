#pragma once
#include "../../tests/utility.hpp"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace browser::css {

    struct GridTrackDef;

    struct EdgeSizes {
        f32 top = 0, right = 0, bottom = 0, left = 0;
    };

    using TextMeasureFn = f32 (*)(void *ctx, const std::string &text, u32 pixel_size);

    struct Rect {
        f32 x = 0, y = 0, width = 0, height = 0;
        std::optional<Rect> intersect(const Rect &o) const;
    };

    class LayoutNode;

    enum class FlexDirection { ROW, ROW_REVERSE, COLUMN, COLUMN_REVERSE };
    enum class FlexWrap { NOWRAP, WRAP, WRAP_REVERSE };
    enum class JustifyContent { FLEX_START, FLEX_END, CENTER, SPACE_BETWEEN, SPACE_AROUND, SPACE_EVENLY };
    enum class AlignItems { STRETCH, FLEX_START, FLEX_END, CENTER, BASELINE };
    enum class AlignSelf { AUTO, STRETCH, FLEX_START, FLEX_END, CENTER, BASELINE };
    enum class AlignContent { FLEX_START, FLEX_END, CENTER, SPACE_BETWEEN, SPACE_AROUND, STRETCH };

    struct Mat3x3 {
        f32 m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

        static Mat3x3 identity() {
            Mat3x3 r;
            return r;
        }
        static Mat3x3 from_values(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f) {
            Mat3x3 r;
            r.m[0][0] = a;
            r.m[0][1] = c;
            r.m[0][2] = e;
            r.m[1][0] = b;
            r.m[1][1] = d;
            r.m[1][2] = f;
            return r;
        }

        Mat3x3 translate(f32 tx, f32 ty) const {
            Mat3x3 r;
            r.m[0][0] = 1;
            r.m[0][1] = 0;
            r.m[0][2] = tx;
            r.m[1][0] = 0;
            r.m[1][1] = 1;
            r.m[1][2] = ty;
            r.m[2][0] = 0;
            r.m[2][1] = 0;
            r.m[2][2] = 1;
            return multiply(r);
        }

        Mat3x3 scale(f32 sx, f32 sy) const {
            Mat3x3 r;
            r.m[0][0] = sx;
            r.m[0][1] = 0;
            r.m[0][2] = 0;
            r.m[1][0] = 0;
            r.m[1][1] = sy;
            r.m[1][2] = 0;
            r.m[2][0] = 0;
            r.m[2][1] = 0;
            r.m[2][2] = 1;
            return multiply(r);
        }

        Mat3x3 rotate(f32 angle) const {
            f32 c = std::cos(angle), s = std::sin(angle);
            Mat3x3 r;
            r.m[0][0] = c;
            r.m[0][1] = -s;
            r.m[0][2] = 0;
            r.m[1][0] = s;
            r.m[1][1] = c;
            r.m[1][2] = 0;
            r.m[2][0] = 0;
            r.m[2][1] = 0;
            r.m[2][2] = 1;
            return multiply(r);
        }

        Mat3x3 skew(f32 ax, f32 ay) const {
            Mat3x3 r;
            r.m[0][0] = 1;
            r.m[0][1] = std::tan(ax);
            r.m[0][2] = 0;
            r.m[1][0] = std::tan(ay);
            r.m[1][1] = 1;
            r.m[1][2] = 0;
            r.m[2][0] = 0;
            r.m[2][1] = 0;
            r.m[2][2] = 1;
            return multiply(r);
        }

        Mat3x3 multiply(const Mat3x3 &o) const {
            Mat3x3 r;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) {
                    r.m[i][j] = 0;
                    for (int k = 0; k < 3; k++) r.m[i][j] += m[i][k] * o.m[k][j];
                }
            return r;
        }
    };

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
        LayoutNode *node = nullptr;
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
        std::vector<FlexItem *> items;
        f32 main_size = 0;
        f32 cross_size = 0;
    };

}  // namespace browser::css
