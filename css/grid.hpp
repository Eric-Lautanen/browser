#pragma once
#include "css_values.hpp"
#include "layout.hpp"

#include <string>
#include <vector>

namespace browser::css {

    enum class GridTrackType { FIXED, FLEX, AUTO, MINMAX, FIT_CONTENT };

    struct GridTrackDef {
        GridTrackType type;
        Length min_size, max_size;
        f32 resolved_size = 0;
    };

    struct GridPlacement {
        i32 line_start = 0, line_end = 0;
        u32 span = 1;
        bool is_explicit = false;
    };

    struct GridArea {
        GridPlacement column, row;
    };

    std::vector<GridTrackDef> parse_track_list(std::string css_value,
                                               f32 container_size,
                                               f32 font_size,
                                               f32 root_font_size);
    GridPlacement parse_grid_line(const std::string &value);

}  // namespace browser::css
