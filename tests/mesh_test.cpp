#include "test_framework.hpp"
#include "../render/mesh.hpp"

TEST(mesh_add_quad, {
    browser::render::Mesh2D mesh;
    mesh.add_quad(0, 0, 100, 100, 1, 0, 0, 1);
    ASSERT_EQ(mesh.vertex_count(), 4u);
    ASSERT_EQ(mesh.index_count(), 6u);
})

TEST(mesh_add_line, {
    browser::render::Mesh2D mesh;
    mesh.add_line(0, 0, 100, 100, 1, 1, 1, 1, 1);
    ASSERT_EQ(mesh.vertex_count(), 4u);
    ASSERT_EQ(mesh.index_count(), 6u);
})

TEST(mesh_clear, {
    browser::render::Mesh2D mesh;
    mesh.add_quad(0, 0, 10, 10, 1, 0, 0, 1);
    mesh.clear();
    ASSERT_EQ(mesh.vertex_count(), 0u);
    ASSERT_EQ(mesh.index_count(), 0u);
})
