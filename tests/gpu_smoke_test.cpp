#include "test_framework.hpp"
#include "../platform/window.hpp"
#include "../platform/opengl.hpp"
#include "../render/renderer.hpp"
#include "../render/texture.hpp"
#include "../render/text_renderer.hpp"

using namespace browser;

TEST(gpu_window_create, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err()) return true;
    auto& window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto ext = window->get_extent();
    ASSERT(ext.width > 0);
    ASSERT(ext.height > 0);
    window->set_should_close(true);
})

TEST(gpu_renderer_init, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err()) return true;
    auto& window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    auto r = renderer->initialize(320, 240);
    ASSERT(r.is_ok());
    window->set_should_close(true);
})

TEST(gpu_texture_create, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err()) return true;
    auto& window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    render::Texture2D tex;
    u8 tex_data[4];
    tex_data[0] = 128; tex_data[1] = 64; tex_data[2] = 32; tex_data[3] = 16;
    auto r = tex.create(2, 2, tex_data);
    ASSERT(r.is_ok());
    ASSERT(tex.width() == 2);
    ASSERT(tex.height() == 2);
    tex.bind(0);
    window->set_should_close(true);
})

TEST(gpu_texture_update_sub, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err()) return true;
    auto& window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    render::Texture2D tex;
    u8 zero_data[16];
    for (int i = 0; i < 16; i++) zero_data[i] = 0;
    auto r = tex.create(4, 4, zero_data);
    ASSERT(r.is_ok());
    u8 sub_data[4];
    sub_data[0] = 255; sub_data[1] = 255; sub_data[2] = 255; sub_data[3] = 255;
    tex.update_sub(0, 0, 2, 2, sub_data);
    window->set_should_close(true);
})

TEST(gpu_texture_move, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err()) return true;
    auto& window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    render::Texture2D tex;
    u8 pixel[1];
    pixel[0] = 255;
    auto r = tex.create(1, 1, pixel);
    ASSERT(r.is_ok());
    render::Texture2D tex2 = std::move(tex);
    ASSERT(tex2.width() == 1);
    ASSERT(tex2.height() == 1);
    ASSERT(tex.id() == 0);
    window->set_should_close(true);
})

TEST(gpu_render_rect, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err()) return true;
    auto& window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    ASSERT(renderer->initialize(320, 240).is_ok());
    renderer->begin_frame();
    renderer->fill_rect(10, 10, 100, 50, render::Color::RED);
    renderer->stroke_rect(10, 70, 100, 50, render::Color::GREEN);
    renderer->draw_line(10, 130, 200, 130, render::Color::BLUE);
    renderer->end_frame();
    window->swap_buffers();
    window->set_should_close(true);
})

TEST(gpu_text_rendering, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err()) return true;
    auto& window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    ASSERT(renderer->initialize(320, 240).is_ok());
    auto fm = std::make_unique<render::FontManager>();
    auto tr = std::make_unique<render::TextRenderer>();
    bool font_ok = false;
    auto try_load = [&](const char* p) {
        auto r = fm->load_from_file(p);
        if (r.is_ok()) { tr->initialize(r.unwrap()); font_ok = true; }
    };
    try_load("C:\\Windows\\Fonts\\consola.ttf");
    if (!font_ok) try_load("C:\\Windows\\Fonts\\lucon.ttf");
    if (!font_ok) { auto r = tr->initialize(fm.get()); font_ok = r.is_ok(); }
    if (font_ok) {
        renderer->begin_frame();
        tr->render_text(renderer.get(), "GPU Test", 10, 50, render::Color::WHITE, 32);
        renderer->end_frame();
        window->swap_buffers();
    }
    window->set_should_close(true);
})
