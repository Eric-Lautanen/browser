#pragma once
#include "../css/layout.hpp"
#include "../html/dom.hpp"
#include "paint/commands.hpp"
#include <string>
#include <vector>

namespace browser::render {

class SVGRenderer {
public:
    void render_svg(const html::Element* svg_element, DisplayList& commands, const css::Rect& bounds);

private:
    struct PathCommand {
        enum { MOVE, LINE, CUBIC, QUAD, ARC, CLOSE } type;
        f32 args[6] = {};
    };

    css::Color parse_color(const std::string& val) const;
    f32 parse_length(const std::string& val, f32 default_val) const;
    std::vector<PathCommand> parse_path(const std::string& d) const;

    void render_rect(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds);
    void render_circle(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds);
    void render_ellipse(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds);
    void render_line(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds);
    void render_polyline(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds);
    void render_polygon(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds);
    void render_path_elem(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds);
    void render_text(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds);
    void render_group(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds, const html::Document* doc);
    void render_use(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds, const html::Document* doc);

    css::Rect compute_viewport(const html::Element* svg_element) const;
};

} // namespace browser::render
