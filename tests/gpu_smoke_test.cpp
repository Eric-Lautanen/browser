#include "../platform/opengl.hpp"
#include "../platform/window.hpp"
#include "../render/paint_executor.hpp"
#include "../render/renderer.hpp"
#include "../render/text_renderer.hpp"
#include "../render/texture.hpp"
#include "test_framework.hpp"

using namespace browser;

TEST(gpu_window_create, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto ext = window->get_extent();
    ASSERT(ext.width > 0);
    ASSERT(ext.height > 0);
    window->set_should_close(true);
})

TEST(gpu_renderer_init, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    auto r = renderer->initialize(320, 240);
    ASSERT(r.is_ok());
    window->set_should_close(true);
})

TEST(gpu_texture_create, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    render::Texture2D tex;
    u8 tex_data[4];
    tex_data[0] = 128;
    tex_data[1] = 64;
    tex_data[2] = 32;
    tex_data[3] = 16;
    auto r = tex.create(2, 2, tex_data);
    ASSERT(r.is_ok());
    ASSERT(tex.width() == 2);
    ASSERT(tex.height() == 2);
    tex.bind(0);
    window->set_should_close(true);
})

TEST(gpu_texture_update_sub, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    render::Texture2D tex;
    u8 zero_data[16];
    for (int i = 0; i < 16; i++) zero_data[i] = 0;
    auto r = tex.create(4, 4, zero_data);
    ASSERT(r.is_ok());
    u8 sub_data[4];
    sub_data[0] = 255;
    sub_data[1] = 255;
    sub_data[2] = 255;
    sub_data[3] = 255;
    tex.update_sub(0, 0, 2, 2, sub_data);
    window->set_should_close(true);
})

TEST(gpu_texture_move, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
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
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
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
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    ASSERT(renderer->initialize(320, 240).is_ok());
    auto fm = std::make_unique<render::FontManager>();
    auto tr = std::make_unique<render::TextRenderer>();
    auto r = tr->initialize(fm.get());
    if (r.is_ok()) {
        renderer->begin_frame();
        tr->render_text(renderer.get(), "GPU Test", 10, 50, render::Color::WHITE, 32);
        renderer->end_frame();
        window->swap_buffers();
    }
    window->set_should_close(true);
})

// BR-P1/R-P1: the executor persists across frames and reuses canvas textures
// via version-guarded upload instead of recreating them every frame.
static render::PaintCommand make_canvas_cmd(void *canvas_id, u32 version, u8 r, u8 g, u8 b) {
    render::PaintCommand cmd;
    cmd.type = render::PaintCommand::Type::DRAW_CANVAS;
    cmd.rect = {0, 0, 16, 16};
    cmd.color = render::Color::WHITE;
    cmd.canvas_data_w = 2;
    cmd.canvas_data_h = 2;
    cmd.canvas_id = canvas_id;
    cmd.canvas_version = version;
    cmd.canvas_pixels = {r, g, b, 255, r, g, b, 255, r, g, b, 255, r, g, b, 255};
    return cmd;
}

TEST(gpu_executor_canvas_texture_persists_across_frames, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    ASSERT(renderer->initialize(320, 240).is_ok());

    int dummy_canvas = 0;
    render::PaintExecutor exec(renderer.get(), nullptr);

    render::DisplayList list1;
    list1.push(make_canvas_cmd(&dummy_canvas, 4, 255, 0, 0));
    renderer->begin_frame();
    exec.execute(list1);
    renderer->end_frame();
    const render::Texture2D *tex1 = exec.cached_canvas_texture(&dummy_canvas);
    ASSERT(tex1 != nullptr);

    // Second frame with an unchanged canvas: same GL texture must be reused.
    // (The old per-frame executor wiped its cache, forcing a fresh upload.)
    render::DisplayList list2;
    list2.push(make_canvas_cmd(&dummy_canvas, 4, 255, 0, 0));
    renderer->begin_frame();
    exec.execute(list2);
    renderer->end_frame();
    const render::Texture2D *tex2 = exec.cached_canvas_texture(&dummy_canvas);
    ASSERT(tex2 != nullptr);
    ASSERT(tex2 == tex1);
    window->set_should_close(true);
})

TEST(gpu_executor_canvas_version_change_reuses_entry, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    ASSERT(renderer->initialize(320, 240).is_ok());

    int dummy_canvas = 0;
    render::PaintExecutor exec(renderer.get(), nullptr);

    render::DisplayList list1;
    list1.push(make_canvas_cmd(&dummy_canvas, 1, 0, 0, 255));
    renderer->begin_frame();
    exec.execute(list1);
    renderer->end_frame();

    // Canvas content changed -> version bump -> entry updated in place,
    // still one live texture for this id.
    render::DisplayList list2;
    list2.push(make_canvas_cmd(&dummy_canvas, 2, 255, 255, 0));
    renderer->begin_frame();
    exec.execute(list2);
    renderer->end_frame();
    ASSERT(exec.cached_canvas_texture(&dummy_canvas) != nullptr);
    window->set_should_close(true);
})

TEST(gpu_executor_page_invalidation_drops_caches, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    ASSERT(renderer->initialize(320, 240).is_ok());

    int dead_page_canvas = 0;  // address about to be "freed" with its page
    render::PaintExecutor exec(renderer.get(), nullptr);

    render::DisplayList list;
    list.push(make_canvas_cmd(&dead_page_canvas, 7, 0, 255, 0));
    renderer->begin_frame();
    exec.execute(list);
    renderer->end_frame();
    ASSERT(exec.cached_canvas_texture(&dead_page_canvas) != nullptr);

    // Page swap: caches keyed by pointers owned by the freed document go.
    exec.invalidate_page_caches();
    ASSERT(exec.cached_canvas_texture(&dead_page_canvas) == nullptr);

    // A NEW document allocating a canvas at the recycled address must not
    // resolve to the dead page's texture.
    render::DisplayList fresh;
    fresh.push(make_canvas_cmd(&dead_page_canvas, 7, 128, 128, 128));
    renderer->begin_frame();
    exec.execute(fresh);
    renderer->end_frame();
    const render::Texture2D *fresh_tex = exec.cached_canvas_texture(&dead_page_canvas);
    ASSERT(fresh_tex != nullptr);
    window->set_should_close(true);
})
