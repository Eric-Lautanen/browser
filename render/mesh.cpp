#include "mesh.hpp"
#include "../platform/opengl.hpp"
#include <cmath>

namespace browser::render {

namespace pgl = browser::platform;

Mesh2D::Mesh2D() = default;

Mesh2D::~Mesh2D() {
    if (uploaded_) {
        pgl::glDeleteVertexArrays(1, &vao_);
        pgl::glDeleteBuffers(1, &vbo_);
        pgl::glDeleteBuffers(1, &ebo_);
    }
}

Mesh2D::Mesh2D(Mesh2D&& other) noexcept
    : vertices_(std::move(other.vertices_))
    , indices_(std::move(other.indices_))
    , vao_(other.vao_), vbo_(other.vbo_), ebo_(other.ebo_)
    , uploaded_(other.uploaded_)
    , attribs_setup_(other.attribs_setup_) {
    other.vao_ = 0;
    other.vbo_ = 0;
    other.ebo_ = 0;
    other.uploaded_ = false;
    other.attribs_setup_ = false;
}

Mesh2D& Mesh2D::operator=(Mesh2D&& other) noexcept {
    if (this != &other) {
        if (uploaded_) {
            pgl::glDeleteVertexArrays(1, &vao_);
            pgl::glDeleteBuffers(1, &vbo_);
            pgl::glDeleteBuffers(1, &ebo_);
        }
        vertices_ = std::move(other.vertices_);
        indices_ = std::move(other.indices_);
        vao_ = other.vao_; vbo_ = other.vbo_; ebo_ = other.ebo_;
        uploaded_ = other.uploaded_;
        attribs_setup_ = other.attribs_setup_;
        other.vao_ = 0; other.vbo_ = 0; other.ebo_ = 0;
        other.uploaded_ = false;
        other.attribs_setup_ = false;
    }
    return *this;
}

void Mesh2D::setup_attribs() {
    pgl::glBindVertexArray(vao_);
    pgl::glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    pgl::glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (const void*)offsetof(Vertex2D, x));
    pgl::glEnableVertexAttribArray(0);
    pgl::glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (const void*)offsetof(Vertex2D, r));
    pgl::glEnableVertexAttribArray(1);
    pgl::glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (const void*)offsetof(Vertex2D, u));
    pgl::glEnableVertexAttribArray(2);
    attribs_setup_ = true;
}

void Mesh2D::upload() {
    bool first_init = !uploaded_;
    if (!uploaded_) {
        pgl::glGenVertexArrays(1, &vao_);
        pgl::glGenBuffers(1, &vbo_);
        pgl::glGenBuffers(1, &ebo_);
        uploaded_ = true;
    }

    pgl::glBindVertexArray(vao_);

    GLsizeiptr vb_size = (GLsizeiptr)(vertices_.size() * sizeof(Vertex2D));

    pgl::glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (vb_size > (GLsizeiptr)kBufferCapacity) {
        pgl::glBufferData(GL_ARRAY_BUFFER, vb_size, nullptr, GL_DYNAMIC_DRAW);
        pgl::glBufferSubData(GL_ARRAY_BUFFER, 0, vb_size, vertices_.data());
        // Re-setup attribs since glBufferData with a larger size orphaned the old storage
        setup_attribs();
    } else if (vb_size > 0) {
        if (first_init) {
            pgl::glBufferData(GL_ARRAY_BUFFER, kBufferCapacity, nullptr, GL_DYNAMIC_DRAW);
        }
        pgl::glBufferSubData(GL_ARRAY_BUFFER, 0, vb_size, vertices_.data());
    }

    pgl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    GLsizeiptr ib_size = (GLsizeiptr)(indices_.size() * sizeof(u32));
    if (ib_size > 0) {
        if (first_init) {
            pgl::glBufferData(GL_ELEMENT_ARRAY_BUFFER, kBufferCapacity, nullptr, GL_DYNAMIC_DRAW);
        }
        pgl::glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ib_size, indices_.data());
    }

    if (!attribs_setup_) {
        setup_attribs();
    }
}

void Mesh2D::bind() const {
    if (uploaded_) pgl::glBindVertexArray(vao_);
}

void Mesh2D::unbind() const {
    pgl::glBindVertexArray(0);
}

void Mesh2D::draw() const {
    if (!uploaded_ || indices_.empty()) return;
    pgl::glBindVertexArray(vao_);
    pgl::glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, 0);
}

void Mesh2D::add_quad(f32 x, f32 y, f32 w, f32 h, f32 r, f32 g, f32 b, f32 a) {
    add_quad_tex(x, y, w, h, r, g, b, a, 0.0f, 0.0f, 1.0f, 1.0f);
}

void Mesh2D::add_quad_tex(f32 x, f32 y, f32 w, f32 h,
                           f32 r, f32 g, f32 b, f32 a,
                           f32 u0, f32 v0, f32 u1, f32 v1) {
    u32 start = (u32)vertices_.size();

    vertices_.push_back({x,     y,     r, g, b, a, u0, v0});
    vertices_.push_back({x + w, y,     r, g, b, a, u1, v0});
    vertices_.push_back({x + w, y + h, r, g, b, a, u1, v1});
    vertices_.push_back({x,     y + h, r, g, b, a, u0, v1});

    indices_.push_back(start);
    indices_.push_back(start + 1);
    indices_.push_back(start + 2);
    indices_.push_back(start);
    indices_.push_back(start + 2);
    indices_.push_back(start + 3);
}

void Mesh2D::add_quad_tex_skewed(f32 x, f32 y, f32 w, f32 h, f32 skew,
                                  f32 r, f32 g, f32 b, f32 a,
                                  f32 u0, f32 v0, f32 u1, f32 v1) {
    u32 start = (u32)vertices_.size();
    // Top edge shifted right by skew, bottom edge at x
    vertices_.push_back({x + skew, y,     r, g, b, a, u0, v0});
    vertices_.push_back({x + w + skew, y, r, g, b, a, u1, v0});
    vertices_.push_back({x + w,     y + h, r, g, b, a, u1, v1});
    vertices_.push_back({x,         y + h, r, g, b, a, u0, v1});

    indices_.push_back(start);
    indices_.push_back(start + 1);
    indices_.push_back(start + 2);
    indices_.push_back(start);
    indices_.push_back(start + 2);
    indices_.push_back(start + 3);
}

void Mesh2D::add_line(f32 x1, f32 y1, f32 x2, f32 y2,
                       f32 r, f32 g, f32 b, f32 a, f32 width) {
    f32 dx = x2 - x1;
    f32 dy = y2 - y1;
    f32 len_sq = dx * dx + dy * dy;
    if (len_sq < 0.0001f) return;
    f32 inv_len = 1.0f / std::sqrt(len_sq);

    f32 nx = -dy * inv_len * width * 0.5f;
    f32 ny =  dx * inv_len * width * 0.5f;

    f32 u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    u32 start = (u32)vertices_.size();

    vertices_.push_back({x1 + nx, y1 + ny, r, g, b, a, u0, v0});
    vertices_.push_back({x2 + nx, y2 + ny, r, g, b, a, u1, v0});
    vertices_.push_back({x2 - nx, y2 - ny, r, g, b, a, u1, v1});
    vertices_.push_back({x1 - nx, y1 - ny, r, g, b, a, u0, v1});

    indices_.push_back(start);
    indices_.push_back(start + 1);
    indices_.push_back(start + 2);
    indices_.push_back(start);
    indices_.push_back(start + 2);
    indices_.push_back(start + 3);
}

void Mesh2D::clear() {
    vertices_.clear();
    indices_.clear();
}

}
