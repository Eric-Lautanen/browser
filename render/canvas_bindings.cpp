#include "../js/dom_bindings.hpp"
#include "../js/vm.hpp"
#include "../js/gc.hpp"
#include "../html/dom.hpp"
#include "canvas.hpp"
#include <unordered_map>
#include <cstdlib>
#include <cmath>

namespace browser::render {

struct CanvasBindingCtx {
    html::Element* element;
    js::DOMBindings* bindings;
    js::VM* vm;
    std::shared_ptr<Canvas2D> canvas;
};

static std::unordered_map<html::Element*, std::shared_ptr<Canvas2D>> canvas_binding_map;
static std::vector<std::unique_ptr<CanvasBindingCtx>> canvas_binding_ctxs;

static CanvasBindingCtx* get_bc(void* context) {
    return static_cast<CanvasBindingCtx*>(context);
}

static js::JSValue canvas_fill_rect_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 5) return js::JSValue::undefined();
    bc->canvas->fill_rect(
        static_cast<f32>(args[1].to_number()),
        static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()),
        static_cast<f32>(args[4].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_stroke_rect_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 5) return js::JSValue::undefined();
    bc->canvas->stroke_rect(
        static_cast<f32>(args[1].to_number()),
        static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()),
        static_cast<f32>(args[4].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_clear_rect_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 5) return js::JSValue::undefined();
    bc->canvas->clear_rect(
        static_cast<f32>(args[1].to_number()),
        static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()),
        static_cast<f32>(args[4].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_fill_text_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 4) return js::JSValue::undefined();
    bc->canvas->fill_text(args[1].to_string(),
        static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_stroke_text_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 4) return js::JSValue::undefined();
    bc->canvas->stroke_text(args[1].to_string(),
        static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_begin_path_fn(const std::vector<js::JSValue>&, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas) return js::JSValue::undefined();
    bc->canvas->begin_path();
    return js::JSValue::undefined();
}

static js::JSValue canvas_move_to_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 3) return js::JSValue::undefined();
    bc->canvas->move_to(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_line_to_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 3) return js::JSValue::undefined();
    bc->canvas->line_to(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_arc_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 6) return js::JSValue::undefined();
    bool ccw = (args.size() > 6 && args[6].is_truthy());
    bc->canvas->arc(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()), static_cast<f32>(args[4].to_number()),
        static_cast<f32>(args[5].to_number()), ccw);
    return js::JSValue::undefined();
}

static js::JSValue canvas_bezier_curve_to_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 7) return js::JSValue::undefined();
    bc->canvas->bezier_curve_to(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()), static_cast<f32>(args[4].to_number()),
        static_cast<f32>(args[5].to_number()), static_cast<f32>(args[6].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_quadratic_curve_to_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 5) return js::JSValue::undefined();
    bc->canvas->quadratic_curve_to(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()), static_cast<f32>(args[4].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_close_path_fn(const std::vector<js::JSValue>&, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas) return js::JSValue::undefined();
    bc->canvas->close_path();
    return js::JSValue::undefined();
}

static js::JSValue canvas_fill_fn(const std::vector<js::JSValue>&, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas) return js::JSValue::undefined();
    bc->canvas->fill();
    return js::JSValue::undefined();
}

static js::JSValue canvas_stroke_fn(const std::vector<js::JSValue>&, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas) return js::JSValue::undefined();
    bc->canvas->stroke();
    return js::JSValue::undefined();
}

static js::JSValue canvas_save_fn(const std::vector<js::JSValue>&, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas) return js::JSValue::undefined();
    bc->canvas->save();
    return js::JSValue::undefined();
}

static js::JSValue canvas_restore_fn(const std::vector<js::JSValue>&, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas) return js::JSValue::undefined();
    bc->canvas->restore();
    return js::JSValue::undefined();
}

static js::JSValue canvas_translate_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 3) return js::JSValue::undefined();
    bc->canvas->translate(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_rotate_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 2) return js::JSValue::undefined();
    bc->canvas->rotate(static_cast<f32>(args[1].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_scale_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 3) return js::JSValue::undefined();
    bc->canvas->scale(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_transform_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 7) return js::JSValue::undefined();
    bc->canvas->transform(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()), static_cast<f32>(args[4].to_number()),
        static_cast<f32>(args[5].to_number()), static_cast<f32>(args[6].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_set_transform_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 7) return js::JSValue::undefined();
    bc->canvas->set_transform(static_cast<f32>(args[1].to_number()), static_cast<f32>(args[2].to_number()),
        static_cast<f32>(args[3].to_number()), static_cast<f32>(args[4].to_number()),
        static_cast<f32>(args[5].to_number()), static_cast<f32>(args[6].to_number()));
    return js::JSValue::undefined();
}

static js::JSValue canvas_get_image_data_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 5) return js::JSValue::undefined();
    u32 sx = static_cast<u32>(args[1].to_number());
    u32 sy = static_cast<u32>(args[2].to_number());
    u32 sw = static_cast<u32>(args[3].to_number());
    u32 sh = static_cast<u32>(args[4].to_number());
    std::vector<u8> pixel_data;
    bc->canvas->get_image_data(sx, sy, sw, sh, pixel_data);
    auto* img_gc = bc->vm->heap()->alloc_object();
    auto* img_obj = &img_gc->obj;
    img_obj->set("width", js::JSValue::number(static_cast<f64>(sw)));
    img_obj->set("height", js::JSValue::number(static_cast<f64>(sh)));
    return js::JSValue::object(img_obj);
}

static js::JSValue canvas_put_image_data_fn(const std::vector<js::JSValue>&, void*) {
    return js::JSValue::undefined();
}

static js::JSValue canvas_to_data_url_fn(const std::vector<js::JSValue>&, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas) return js::JSValue::string("");
    return js::JSValue::string(bc->canvas->to_data_url());
}

static js::JSValue canvas_set_fill_style_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 2) return js::JSValue::undefined();
    std::string color_str = args[1].to_string();
    f32 r = 0, g = 0, b = 0, a = 1;
    if (!color_str.empty() && color_str[0] == '#') {
        if (color_str.size() == 7) {
            unsigned long hex = std::strtoul(color_str.c_str() + 1, nullptr, 16);
            r = ((hex >> 16) & 0xFF) / 255.0f;
            g = ((hex >> 8) & 0xFF) / 255.0f;
            b = (hex & 0xFF) / 255.0f;
        } else if (color_str.size() == 4) {
            unsigned long hex = std::strtoul(color_str.c_str() + 1, nullptr, 16);
            r = ((hex >> 8) & 0xF) / 15.0f;
            g = ((hex >> 4) & 0xF) / 15.0f;
            b = (hex & 0xF) / 15.0f;
        }
    }
    bc->canvas->set_fill_style(r, g, b, a);
    return js::JSValue::undefined();
}

static js::JSValue canvas_set_stroke_style_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* bc = get_bc(context);
    if (!bc->canvas || args.size() < 2) return js::JSValue::undefined();
    std::string color_str = args[1].to_string();
    f32 r = 0, g = 0, b = 0, a = 1;
    if (!color_str.empty() && color_str[0] == '#') {
        if (color_str.size() == 7) {
            unsigned long hex = std::strtoul(color_str.c_str() + 1, nullptr, 16);
            r = ((hex >> 16) & 0xFF) / 255.0f;
            g = ((hex >> 8) & 0xFF) / 255.0f;
            b = (hex & 0xFF) / 255.0f;
        } else if (color_str.size() == 4) {
            unsigned long hex = std::strtoul(color_str.c_str() + 1, nullptr, 16);
            r = ((hex >> 8) & 0xF) / 15.0f;
            g = ((hex >> 4) & 0xF) / 15.0f;
            b = (hex & 0xF) / 15.0f;
        }
    }
    bc->canvas->set_stroke_style(r, g, b, a);
    return js::JSValue::undefined();
}

static js::JSValue canvas_get_context_fn(const std::vector<js::JSValue>& args, void* context) {
    auto* el_ctx = static_cast<CanvasBindingCtx*>(context);
    if (args.size() < 2) return js::JSValue::null();
    std::string type = args[1].to_string();
    if (type != "2d") return js::JSValue::null();

    auto map_it = canvas_binding_map.find(el_ctx->element);
    if (map_it != canvas_binding_map.end()) return js::JSValue::null();

    u32 w = 300, h = 150;
    if (el_ctx->element->has_attribute("width")) {
        char* end = nullptr;
        long val = std::strtol(el_ctx->element->get_attribute("width").c_str(), &end, 10);
        if (end && val > 0 && val < 65536) w = static_cast<u32>(val);
    }
    if (el_ctx->element->has_attribute("height")) {
        char* end = nullptr;
        long val = std::strtol(el_ctx->element->get_attribute("height").c_str(), &end, 10);
        if (end && val > 0 && val < 65536) h = static_cast<u32>(val);
    }

    auto canvas2d = std::make_shared<Canvas2D>(w, h);
    canvas_binding_map[el_ctx->element] = canvas2d;
    g_canvas_registry[el_ctx->element] = canvas2d;

    auto* ctx2d = new CanvasBindingCtx{el_ctx->element, el_ctx->bindings, el_ctx->vm, canvas2d};
    canvas_binding_ctxs.push_back(std::unique_ptr<CanvasBindingCtx>(ctx2d));

    auto* ctx_gc = ctx2d->vm->heap()->alloc_object();
    auto* ctx_obj = &ctx_gc->obj;

    auto add_fn = [&](const char* name, js::JSFunction::NativeFn fn) {
        ctx_obj->set(name, js::JSValue::function(ctx2d->vm->create_native_fn(fn, false, ctx2d)));
    };

    add_fn("fillRect", canvas_fill_rect_fn);
    add_fn("strokeRect", canvas_stroke_rect_fn);
    add_fn("clearRect", canvas_clear_rect_fn);
    add_fn("fillText", canvas_fill_text_fn);
    add_fn("strokeText", canvas_stroke_text_fn);
    add_fn("beginPath", canvas_begin_path_fn);
    add_fn("moveTo", canvas_move_to_fn);
    add_fn("lineTo", canvas_line_to_fn);
    add_fn("arc", canvas_arc_fn);
    add_fn("bezierCurveTo", canvas_bezier_curve_to_fn);
    add_fn("quadraticCurveTo", canvas_quadratic_curve_to_fn);
    add_fn("closePath", canvas_close_path_fn);
    add_fn("fill", canvas_fill_fn);
    add_fn("stroke", canvas_stroke_fn);
    add_fn("save", canvas_save_fn);
    add_fn("restore", canvas_restore_fn);
    add_fn("translate", canvas_translate_fn);
    add_fn("rotate", canvas_rotate_fn);
    add_fn("scale", canvas_scale_fn);
    add_fn("transform", canvas_transform_fn);
    add_fn("setTransform", canvas_set_transform_fn);
    add_fn("getImageData", canvas_get_image_data_fn);
    add_fn("putImageData", canvas_put_image_data_fn);
    add_fn("toDataURL", canvas_to_data_url_fn);
    add_fn("setFillStyle", canvas_set_fill_style_fn);
    add_fn("setStrokeStyle", canvas_set_stroke_style_fn);

    return js::JSValue::object(ctx_obj);
}

void register_canvas_element_bindings(js::DOMBindings* bindings, js::VM* vm, html::Element* element) {
    if (element->tag_name != "canvas") return;

    auto* canvas_ctx = new CanvasBindingCtx{element, bindings, vm, nullptr};
    canvas_binding_ctxs.push_back(std::unique_ptr<CanvasBindingCtx>(canvas_ctx));
    auto* gc_obj = vm->heap()->alloc_object();
    auto* obj = &gc_obj->obj;
    obj->set("getContext",
             js::JSValue::function(vm->create_native_fn(canvas_get_context_fn, false, canvas_ctx)));
}

void setup_canvas_bindings() {
    js::DOMBindings::set_element_extender(register_canvas_element_bindings);
}

} // namespace browser::render
