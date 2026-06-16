#include "renderer.hpp"
#include "icons.hpp"
#include "texture.hpp"
#include "shaders.hpp"

namespace browser::render {

namespace pgl = browser::platform;

const Color Color::RED   = {1.0f, 0.0f, 0.0f, 1.0f};
const Color Color::GREEN = {0.0f, 1.0f, 0.0f, 1.0f};
const Color Color::BLUE  = {0.0f, 0.0f, 1.0f, 1.0f};
const Color Color::WHITE = {1.0f, 1.0f, 1.0f, 1.0f};
const Color Color::BLACK = {0.0f, 0.0f, 0.0f, 1.0f};
const Color Color::CYAN = {0.0f, 1.0f, 1.0f, 1.0f};
const Color Color::TRANSPARENT = {0.0f, 0.0f, 0.0f, 0.0f};

Mat4 Mat4::ortho(f32 left, f32 right, f32 bottom, f32 top) {
    Mat4 m = {};
    m.data[0]  = 2.0f / (right - left);
    m.data[5]  = 2.0f / (top - bottom);
    m.data[10] = -1.0f;
    m.data[12] = -(right + left) / (right - left);
    m.data[13] = -(top + bottom) / (top - bottom);
    m.data[15] = 1.0f;
    return m;
}

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

Result<void> Renderer::initialize(u32 window_width, u32 window_height) {
    shader_ = std::make_unique<ShaderProgram>();
    batch_mesh_ = std::make_unique<Mesh2D>();

    auto r = shader_->compile(BASIC_VERTEX_SHADER, BASIC_FRAGMENT_SHADER);
    if (r.is_err()) return r;

    width_ = window_width;
    height_ = window_height;

    pgl::glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    pgl::glEnable(GL_BLEND);
    pgl::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize projection matrix to avoid uninitialized uniform
    set_viewport(window_width, window_height);

    initialized_ = true;

    icon_atlas_ = std::make_unique<IconAtlas>();
    auto icon_r = icon_atlas_->initialize();
    if (icon_r.is_err()) return icon_r;

    return {};
}

void Renderer::flush() {
    if (batch_mesh_->vertex_count() == 0) return;
    batch_mesh_->upload();
    batch_mesh_->draw();
    batch_mesh_->clear();
}

void Renderer::begin_frame() {
    pgl::glClear(GL_COLOR_BUFFER_BIT);
    batch_mesh_->clear();
    current_texture_id_ = 0;
    textured_mode_ = false;
}

void Renderer::end_frame() {
    if (textured_mode_) flush();
    shader_->bind();
    const auto& u = shader_->uniforms();
    if (u.texture >= 0) pgl::glUniform1i(u.texture, 0);
    if (u.use_texture >= 0) pgl::glUniform1i(u.use_texture, 0);
    flush();
    textured_mode_ = false;
    current_texture_id_ = 0;
}

void Renderer::fill_rect(f32 x, f32 y, f32 w, f32 h, const Color& color) {
    if (textured_mode_) end_textured();
    batch_mesh_->add_quad(x, y, w, h, color.r, color.g, color.b, color.a);
}

void Renderer::stroke_rect(f32 x, f32 y, f32 w, f32 h, const Color& color, f32 line_width) {
    if (line_width <= 0.0f) return;
    if (textured_mode_) end_textured();
    f32 r = color.r, g = color.g, b = color.b, a = color.a;

    // Simple 4-edge stroke: draw 4 axis-aligned quads (one per edge).
    // Top
    batch_mesh_->add_quad(x, y, w, line_width, r, g, b, a);
    // Bottom
    batch_mesh_->add_quad(x, y + h - line_width, w, line_width, r, g, b, a);
    // Left
    batch_mesh_->add_quad(x, y, line_width, h, r, g, b, a);
    // Right
    batch_mesh_->add_quad(x + w - line_width, y, line_width, h, r, g, b, a);
}

void Renderer::draw_line(f32 x1, f32 y1, f32 x2, f32 y2, const Color& color, f32 line_width) {
    if (textured_mode_) end_textured();
    batch_mesh_->add_line(x1, y1, x2, y2, color.r, color.g, color.b, color.a, line_width);
}

void Renderer::fill_rect_tex(f32 x, f32 y, f32 w, f32 h, const Color& color, Texture2D* texture) {
    draw_textured_quad(x, y, w, h, color, texture);
}

void Renderer::draw_textured_quad(f32 x, f32 y, f32 w, f32 h, const Color& color, Texture2D* texture) {
    if (!texture) {
        fill_rect(x, y, w, h, color);
        return;
    }
    u32 tid = texture->id();
    if (!textured_mode_ || current_texture_id_ != tid) {
        flush();
        shader_->bind();
        const auto& u = shader_->uniforms();
        if (u.use_texture >= 0) pgl::glUniform1i(u.use_texture, 1);
        texture->bind(0);
        current_texture_id_ = tid;
        textured_mode_ = true;
    }
    batch_mesh_->add_quad_tex(x, y, w, h, color.r, color.g, color.b, color.a, 0.0f, 0.0f, 1.0f, 1.0f);
}

void Renderer::begin_textured(Texture2D* texture) {
    flush();
    shader_->bind();
    const auto& u = shader_->uniforms();
    if (u.use_texture >= 0) pgl::glUniform1i(u.use_texture, 1);
    texture->bind(0);
    current_texture_id_ = texture->id();
    textured_mode_ = true;
}

void Renderer::add_tex_quad(f32 x, f32 y, f32 w, f32 h, const Color& color, f32 u0, f32 v0, f32 u1, f32 v1) {
    batch_mesh_->add_quad_tex(x, y, w, h, color.r, color.g, color.b, color.a, u0, v0, u1, v1);
}

void Renderer::end_textured() {
    flush();
    textured_mode_ = false;
    current_texture_id_ = 0;
}

void Renderer::draw_icon(Icon icon, f32 x, f32 y, f32 size, const Color& color) {
    if (icon_atlas_) icon_atlas_->draw(this, icon, x, y, size, color);
}

void Renderer::draw_icon_centered(Icon icon, f32 bx, f32 by, f32 bw, f32 bh,
                                  f32 icon_size, const Color& color) {
    if (icon_atlas_) icon_atlas_->draw_centered(this, icon, bx, by, bw, bh, icon_size, color);
}

void Renderer::set_viewport(u32 width, u32 height) {
    width_ = width;
    height_ = height;
    pgl::glViewport(0, 0, (GLsizei)width, (GLsizei)height);

    Mat4 proj = Mat4::ortho(0.0f, (f32)width, (f32)height, 0.0f);
    shader_->bind();
    const auto& u = shader_->uniforms();
    if (u.projection >= 0) pgl::glUniformMatrix4fv(u.projection, 1, GL_FALSE, proj.data);
    if (u.texture >= 0) pgl::glUniform1i(u.texture, 0);
    if (u.use_texture >= 0) pgl::glUniform1i(u.use_texture, 0);
}

}
