#pragma once
#include <vector>
#include "../tests/utility.hpp"
#include "../platform/opengl.hpp"

namespace browser::render {

#if defined(_MSC_VER)
#pragma pack(push, 1)
struct Vertex2D {
#elif defined(__GNUC__) || defined(__clang__)
struct __attribute__((packed)) Vertex2D {
#else
struct Vertex2D {
#endif
    f32 x, y;       // position
    f32 r, g, b, a; // color
    f32 u, v;       // texcoord
};
#if defined(_MSC_VER)
#pragma pack(pop)
#endif

static_assert(offsetof(Vertex2D, x) == 0, "Vertex2D.x offset");
static_assert(offsetof(Vertex2D, y) == 4, "Vertex2D.y offset");
static_assert(offsetof(Vertex2D, r) == 8, "Vertex2D.r offset");
static_assert(offsetof(Vertex2D, g) == 12, "Vertex2D.g offset");
static_assert(offsetof(Vertex2D, b) == 16, "Vertex2D.b offset");
static_assert(offsetof(Vertex2D, a) == 20, "Vertex2D.a offset");
static_assert(offsetof(Vertex2D, u) == 24, "Vertex2D.u offset");
static_assert(offsetof(Vertex2D, v) == 28, "Vertex2D.v offset");
static_assert(sizeof(Vertex2D) == 32, "Vertex2D size");

class Mesh2D {
public:
    Mesh2D();
    ~Mesh2D();
    Mesh2D(Mesh2D&&) noexcept;
    Mesh2D& operator=(Mesh2D&&) noexcept;
    Mesh2D(const Mesh2D&) = delete;

    void upload();
    void bind() const;
    void unbind() const;
    void draw() const;

    void add_quad(f32 x, f32 y, f32 w, f32 h, f32 r, f32 g, f32 b, f32 a);
    void add_quad_tex(f32 x, f32 y, f32 w, f32 h, f32 r, f32 g, f32 b, f32 a,
                      f32 u0, f32 v0, f32 u1, f32 v1);
    void add_line(f32 x1, f32 y1, f32 x2, f32 y2, f32 r, f32 g, f32 b, f32 a, f32 width);
    void clear();
    u32 vertex_count() const { return (u32)vertices_.size(); }
    u32 index_count() const { return (u32)indices_.size(); }

    void setup_attribs();
private:
    std::vector<Vertex2D> vertices_;
    std::vector<u32> indices_;
    u32 vao_ = 0, vbo_ = 0, ebo_ = 0;
    bool uploaded_ = false;
    bool attribs_setup_ = false;
    static constexpr GLsizeiptr kBufferCapacity = 4 * 1024 * 1024; // 4MB
};

}
