#include "svg_renderer.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace browser::render {

static Color to_render_color(const css::Color& c) {
    return {static_cast<f32>(c.r) / 255.0f, static_cast<f32>(c.g) / 255.0f,
            static_cast<f32>(c.b) / 255.0f, static_cast<f32>(c.a) / 255.0f};
}

css::Color SVGRenderer::parse_color(const std::string& val) const {
    auto c = css::Color::from_name(val);
    if (c.a != 0 || val == "transparent") return c;
    if (!val.empty() && val[0] == '#') {
        c = css::Color::from_hex(val);
        if (c.a != 0 || val == "#000000") return c;
    }
    return {0, 0, 0, 255};
}

f32 SVGRenderer::parse_length(const std::string& val, f32 default_val) const {
    if (val.empty()) return default_val;
    char* end = nullptr;
    f32 result = std::strtof(val.c_str(), &end);
    if (end != val.c_str()) return result;
    return default_val;
}

void SVGRenderer::render_svg(const html::Element* svg_element, DisplayList& commands, const css::Rect&) {
    if (!svg_element) return;
    auto vp = compute_viewport(svg_element);

    for (auto& child : svg_element->children) {
        if (child->type != html::NodeType::ELEMENT) continue;
        auto* el = static_cast<const html::Element*>(child.get());
        const auto& tag = el->tag_name;

        if (tag == "g") {
            render_group(el, commands, vp, nullptr);
        } else if (tag == "rect") {
            render_rect(el, commands, vp);
        } else if (tag == "circle") {
            render_circle(el, commands, vp);
        } else if (tag == "ellipse") {
            render_ellipse(el, commands, vp);
        } else if (tag == "line") {
            render_line(el, commands, vp);
        } else if (tag == "polyline") {
            render_polyline(el, commands, vp);
        } else if (tag == "polygon") {
            render_polygon(el, commands, vp);
        } else if (tag == "path") {
            render_path_elem(el, commands, vp);
        } else if (tag == "text") {
            render_text(el, commands, vp);
        } else if (tag == "use") {
            render_use(el, commands, vp, nullptr);
        }
    }
}

css::Rect SVGRenderer::compute_viewport(const html::Element* svg_element) const {
    css::Rect r = {0, 0, 300, 150};
    std::string w = svg_element->get_attribute("width");
    std::string h = svg_element->get_attribute("height");
    if (!w.empty()) r.width = parse_length(w, 300);
    if (!h.empty()) r.height = parse_length(h, 150);
    return r;
}

void SVGRenderer::render_rect(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds) {
    f32 x = parse_length(el->get_attribute("x"), 0) + svg_bounds.x;
    f32 y = parse_length(el->get_attribute("y"), 0) + svg_bounds.y;
    f32 w = parse_length(el->get_attribute("width"), 0);
    f32 h = parse_length(el->get_attribute("height"), 0);
    f32 rx = parse_length(el->get_attribute("rx"), 0);

    auto fill_col = parse_color(el->get_attribute("fill"));
    auto stroke_col = parse_color(el->get_attribute("stroke"));
    f32 stroke_w = parse_length(el->get_attribute("stroke-width"), 0);

    if (fill_col.a > 0) {
        if (rx > 0) {
            PaintCommand cmd;
            cmd.type = PaintCommand::Type::DRAW_ROUNDED_RECT;
            cmd.rect = {x, y, w, h};
            cmd.color = to_render_color(fill_col);
            cmd.radius = rx;
            cmds.push(cmd);
        } else {
            PaintCommand cmd;
            cmd.type = PaintCommand::Type::FILL_RECT;
            cmd.rect = {x, y, w, h};
            cmd.color = to_render_color(fill_col);
            cmds.push(cmd);
        }
    }
    if (stroke_col.a > 0 && stroke_w > 0) {
        PaintCommand cmd;
        cmd.type = PaintCommand::Type::DRAW_ROUNDED_RECT;
        cmd.rect = {x, y, w, h};
        cmd.color = to_render_color(stroke_col);
        cmd.radius = rx;
        cmds.push(cmd);
    }
}

void SVGRenderer::render_circle(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds) {
    f32 cx = parse_length(el->get_attribute("cx"), 0) + svg_bounds.x;
    f32 cy = parse_length(el->get_attribute("cy"), 0) + svg_bounds.y;
    f32 r = parse_length(el->get_attribute("r"), 0);

    auto fill_col = parse_color(el->get_attribute("fill"));

    if (fill_col.a > 0) {
        PaintCommand cmd;
        cmd.type = PaintCommand::Type::DRAW_ROUNDED_RECT;
        cmd.rect = {cx - r, cy - r, 2 * r, 2 * r};
        cmd.color = to_render_color(fill_col);
        cmd.radius = r;
        cmds.push(cmd);
    }
}

void SVGRenderer::render_ellipse(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds) {
    f32 cx = parse_length(el->get_attribute("cx"), 0) + svg_bounds.x;
    f32 cy = parse_length(el->get_attribute("cy"), 0) + svg_bounds.y;
    f32 rx = parse_length(el->get_attribute("rx"), 0);
    f32 ry = parse_length(el->get_attribute("ry"), 0);

    auto fill_col = parse_color(el->get_attribute("fill"));

    if (fill_col.a > 0) {
        PaintCommand cmd;
        cmd.type = PaintCommand::Type::DRAW_ROUNDED_RECT;
        cmd.rect = {cx - rx, cy - ry, 2 * rx, 2 * ry};
        cmd.color = to_render_color(fill_col);
        cmd.radius = std::min(rx, ry);
        cmds.push(cmd);
    }
}

void SVGRenderer::render_line(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds) {
    f32 x1 = parse_length(el->get_attribute("x1"), 0) + svg_bounds.x;
    f32 y1 = parse_length(el->get_attribute("y1"), 0) + svg_bounds.y;
    f32 x2 = parse_length(el->get_attribute("x2"), 0) + svg_bounds.x;
    f32 y2 = parse_length(el->get_attribute("y2"), 0) + svg_bounds.y;
    auto stroke_col = parse_color(el->get_attribute("stroke"));
    f32 stroke_w = parse_length(el->get_attribute("stroke-width"), 1);

    if (stroke_col.a > 0) {
        PaintCommand cmd;
        cmd.type = PaintCommand::Type::FILL_RECT;
        f32 dx = x2 - x1, dy = y2 - y1;
        f32 len = std::sqrt(dx * dx + dy * dy);
        if (len > 0) {
            cmd.rect = {x1, y1 - stroke_w / 2, len, stroke_w};
            cmd.color = to_render_color(stroke_col);
            cmds.push(cmd);
        }
    }
}

void SVGRenderer::render_polyline(const html::Element* el, DisplayList&, const css::Rect&) {
    std::string pts = el->get_attribute("points");
    if (pts.empty()) return;
    (void)pts;
}

void SVGRenderer::render_polygon(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds) {
    render_polyline(el, cmds, svg_bounds);
}

void SVGRenderer::render_path_elem(const html::Element* el, DisplayList&, const css::Rect&) {
    std::string d = el->get_attribute("d");
    if (d.empty()) return;
    (void)d;
}

void SVGRenderer::render_text(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds) {
    f32 x = parse_length(el->get_attribute("x"), 0) + svg_bounds.x;
    f32 y = parse_length(el->get_attribute("y"), 0) + svg_bounds.y;
    f32 font_size = parse_length(el->get_attribute("font-size"), 16);
    auto fill_col = parse_color(el->get_attribute("fill"));
    if (fill_col.a == 0) fill_col = {0, 0, 0, 255};

    std::string text;
    for (auto& child : el->children) {
        if (child->type == html::NodeType::TEXT) {
            text += static_cast<html::Text*>(child.get())->data;
        }
    }

    if (!text.empty()) {
        PaintCommand cmd;
        cmd.type = PaintCommand::Type::DRAW_TEXT;
        cmd.rect = {x, y, 200, font_size * 1.2f};
        cmd.color = to_render_color(fill_col);
        cmd.text = text;
        cmd.font_size = font_size;
        cmds.push(cmd);
    }
}

void SVGRenderer::render_group(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds, const html::Document* doc) {
    for (auto& child : el->children) {
        if (child->type != html::NodeType::ELEMENT) continue;
        auto* child_el = static_cast<const html::Element*>(child.get());
        const auto& tag = child_el->tag_name;
        if (tag == "rect") render_rect(child_el, cmds, svg_bounds);
        else if (tag == "circle") render_circle(child_el, cmds, svg_bounds);
        else if (tag == "ellipse") render_ellipse(child_el, cmds, svg_bounds);
        else if (tag == "line") render_line(child_el, cmds, svg_bounds);
        else if (tag == "polyline") render_polyline(child_el, cmds, svg_bounds);
        else if (tag == "polygon") render_polygon(child_el, cmds, svg_bounds);
        else if (tag == "path") render_path_elem(child_el, cmds, svg_bounds);
        else if (tag == "text") render_text(child_el, cmds, svg_bounds);
        else if (tag == "g") render_group(child_el, cmds, svg_bounds, doc);
        else if (tag == "use") render_use(child_el, cmds, svg_bounds, doc);
    }
}

void SVGRenderer::render_use(const html::Element* el, DisplayList& cmds, const css::Rect& svg_bounds, const html::Document* doc) {
    (void)el;
    (void)cmds;
    (void)svg_bounds;
    (void)doc;
}

std::vector<SVGRenderer::PathCommand> SVGRenderer::parse_path(const std::string& d) const {
    std::vector<PathCommand> cmds;
    (void)d;
    return cmds;
}

} // namespace browser::render
