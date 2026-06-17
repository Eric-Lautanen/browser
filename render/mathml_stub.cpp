#include "mathml_stub.hpp"
#include <string>

namespace browser::render {

static Color to_render_color(const css::Color& c) {
    return {static_cast<f32>(c.r) / 255.0f, static_cast<f32>(c.g) / 255.0f,
            static_cast<f32>(c.b) / 255.0f, static_cast<f32>(c.a) / 255.0f};
}

void MathMLRenderer::render(const html::Element* math_element, DisplayList& commands, f32 font_size) {
    if (!math_element) return;

    f32 x = 0;
    f32 y = 0;

    for (auto& child : math_element->children) {
        if (child->type != html::NodeType::ELEMENT) {
            if (child->type == html::NodeType::TEXT) {
                auto* text = static_cast<html::Text*>(child.get());
                if (!text->data.empty()) {
                    PaintCommand cmd;
                    cmd.type = PaintCommand::Type::DRAW_TEXT;
                    cmd.rect = {x, y, static_cast<f32>(text->data.size()) * font_size * 0.6f, font_size * 1.2f};
                    cmd.color = to_render_color({0, 0, 0, 255});
                    cmd.text = text->data;
                    cmd.font_size = font_size;
                    commands.push(cmd);
                    x += cmd.rect.width;
                }
            }
            continue;
        }

        auto* el = static_cast<const html::Element*>(child.get());
        const auto& tag = el->tag_name;

        if (tag == "mi") {
            std::string text;
            for (auto& c : el->children) {
                if (c->type == html::NodeType::TEXT) {
                    text += static_cast<html::Text*>(c.get())->data;
                }
            }
            if (text.empty()) text = el->get_attribute("text");
            if (text.empty()) continue;
            PaintCommand cmd;
            cmd.type = PaintCommand::Type::DRAW_TEXT;
            cmd.rect = {x, y, static_cast<f32>(text.size()) * font_size * 0.6f, font_size * 1.2f};
            cmd.color = to_render_color({0, 0, 0, 255});
            cmd.text = text;
            cmd.font_size = font_size;
            commands.push(cmd);
            x += cmd.rect.width;
        } else if (tag == "mn") {
            std::string text;
            for (auto& c : el->children) {
                if (c->type == html::NodeType::TEXT) {
                    text += static_cast<html::Text*>(c.get())->data;
                }
            }
            if (text.empty()) continue;
            PaintCommand cmd;
            cmd.type = PaintCommand::Type::DRAW_TEXT;
            cmd.rect = {x, y, static_cast<f32>(text.size()) * font_size * 0.6f, font_size * 1.2f};
            cmd.color = to_render_color({0, 0, 0, 255});
            cmd.text = text;
            cmd.font_size = font_size;
            commands.push(cmd);
            x += cmd.rect.width;
        } else if (tag == "mo") {
            std::string text;
            for (auto& c : el->children) {
                if (c->type == html::NodeType::TEXT) {
                    text += static_cast<html::Text*>(c.get())->data;
                }
            }
            if (text.empty()) text = el->get_attribute("text");
            if (text.empty()) continue;
            PaintCommand cmd;
            cmd.type = PaintCommand::Type::DRAW_TEXT;
            cmd.rect = {x, y, static_cast<f32>(text.size()) * font_size * 0.6f, font_size * 1.2f};
            cmd.color = to_render_color({0, 0, 0, 255});
            cmd.text = text;
            cmd.font_size = font_size;
            commands.push(cmd);
            x += cmd.rect.width;
        } else if (tag == "msub" || tag == "msup") {
            // Simplified: render children inline
            render(el, commands, font_size);
        } else if (tag == "mfrac") {
            f32 line_y = y + font_size;
            PaintCommand line;
            line.type = PaintCommand::Type::FILL_RECT;
            line.rect = {x, line_y, 40, 1};
            line.color = to_render_color({0, 0, 0, 255});
            commands.push(line);
            render(el, commands, font_size * 0.7f);
        } else if (tag == "msqrt") {
            // Simplified: render with a square root symbol
            PaintCommand sqrt_cmd;
            sqrt_cmd.type = PaintCommand::Type::DRAW_TEXT;
            sqrt_cmd.rect = {x, y, font_size, font_size};
            sqrt_cmd.color = to_render_color({0, 0, 0, 255});
            sqrt_cmd.text = "\xE2\x88\x9A"; // √
            sqrt_cmd.font_size = font_size;
            commands.push(sqrt_cmd);
            x += font_size;
            render(el, commands, font_size);
        } else {
            // Fallback: render children inline for unknown elements
            render(el, commands, font_size);
        }
    }
}

} // namespace browser::render
