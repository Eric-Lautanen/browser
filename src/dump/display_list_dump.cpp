#include "display_list_dump.hpp"
#include "json_writer.hpp"
#include "../../core/utility.hpp"
#include "../../render/paint.hpp"

using browser::f32;
using browser::u32;
// ---------------------------------------------------------------------------
// Display list dump
// ---------------------------------------------------------------------------
std::string paint_cmd_type_str(browser::render::PaintCommand::Type t) {
    switch (t) {
        case browser::render::PaintCommand::Type::FILL_RECT:
            return "FILL_RECT";
        case browser::render::PaintCommand::Type::DRAW_TEXT:
            return "DRAW_TEXT";
        case browser::render::PaintCommand::Type::PUSH_CLIP:
            return "PUSH_CLIP";
        case browser::render::PaintCommand::Type::POP_CLIP:
            return "POP_CLIP";
        case browser::render::PaintCommand::Type::DRAW_IMAGE:
            return "DRAW_IMAGE";
        case browser::render::PaintCommand::Type::DRAW_GRADIENT:
            return "DRAW_GRADIENT";
        case browser::render::PaintCommand::Type::DRAW_SHADOW:
            return "DRAW_SHADOW";
        case browser::render::PaintCommand::Type::PUSH_TRANSFORM:
            return "PUSH_TRANSFORM";
        case browser::render::PaintCommand::Type::POP_TRANSFORM:
            return "POP_TRANSFORM";
        case browser::render::PaintCommand::Type::PUSH_OPACITY:
            return "PUSH_OPACITY";
        case browser::render::PaintCommand::Type::POP_OPACITY:
            return "POP_OPACITY";
        case browser::render::PaintCommand::Type::DRAW_ROUNDED_RECT:
            return "DRAW_ROUNDED_RECT";
        case browser::render::PaintCommand::Type::DRAW_CANVAS:
            return "DRAW_CANVAS";
        case browser::render::PaintCommand::Type::PUSH_FILTER:
            return "PUSH_FILTER";
        case browser::render::PaintCommand::Type::POP_FILTER:
            return "POP_FILTER";
    }
    return "UNKNOWN";
}

std::string render_color_to_hex(const browser::render::Color &c) {
    char buf[16];
    snprintf(buf,
             sizeof buf,
             "#%02x%02x%02x",
             static_cast<int>(c.r * 255 + 0.5f),
             static_cast<int>(c.g * 255 + 0.5f),
             static_cast<int>(c.b * 255 + 0.5f));
    return buf;
}

std::string dump_command(const browser::render::PaintCommand &cmd) {
    json::Obj o;
    o.kv_raw("cmd", json::q(paint_cmd_type_str(cmd.type)));
    o.kv_num("x", cmd.rect.x);
    o.kv_num("y", cmd.rect.y);
    o.kv_num("w", cmd.rect.width);
    o.kv_num("h", cmd.rect.height);
    if (cmd.type == browser::render::PaintCommand::Type::FILL_RECT ||
        cmd.type == browser::render::PaintCommand::Type::DRAW_ROUNDED_RECT ||
        cmd.type == browser::render::PaintCommand::Type::DRAW_TEXT ||
        cmd.type == browser::render::PaintCommand::Type::DRAW_SHADOW) {
        o.kv_raw("color", json::q(render_color_to_hex(cmd.color)));
    }
    if (cmd.type == browser::render::PaintCommand::Type::DRAW_TEXT) {
        o.kv_raw("text", json::q(cmd.text));
        o.kv_num("font_size", cmd.font_size);
        if (cmd.font_flags)
            o.kv_num("font_flags", static_cast<f32>(cmd.font_flags));
    }
    if (cmd.radius > 0)
        o.kv_num("radius", cmd.radius);
    return o.done();
}

