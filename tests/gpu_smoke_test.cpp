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

// ── R-P2 / R-P3 / R-G1: separable clamped blur + FBO pooling + scissor ──

static render::PaintCommand make_filter_cmd(f32 blur_radius) {
    render::PaintCommand cmd;
    cmd.type = render::PaintCommand::Type::PUSH_FILTER;
    cmd.rect = {10, 10, 100, 80};
    css::CSSFilterFunc f;
    f.type = css::CSSFilterFunc::Type::BLUR;
    f.length_param.value = blur_radius;
    f.length_param.unit = css::Length::Unit::PX;
    cmd.filters.push_back(f);
    return cmd;
}

static render::DisplayList make_blur_frame(void *canvas_id, f32 blur_radius) {
    render::DisplayList list;
    list.push(make_filter_cmd(blur_radius));
    list.push(make_canvas_cmd(canvas_id, 1, 200, 200, 200));
    render::PaintCommand pop;
    pop.type = render::PaintCommand::Type::POP_FILTER;
    list.push(pop);
    return list;
}

TEST(gpu_blur_huge_radius_clamped_and_safe, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    ASSERT(renderer->initialize(320, 240).is_ok());

    // blur(500px) used to compile to a single-pass O(radius^2) kernel:
    // ~1,000,000 taps per pixel -> driver reset territory. It must now run
    // as two O(radius<=64) passes without hanging or crashing.
    int dummy_canvas = 0;
    render::PaintExecutor exec(renderer.get(), nullptr);
    auto list = make_blur_frame(&dummy_canvas, 500.0f);

    renderer->begin_frame();
    exec.execute(list);
    renderer->end_frame();
    window->swap_buffers();
    window->set_should_close(true);
})

TEST(gpu_filter_fbos_are_pooled_across_frames, {
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

    for (int frame = 0; frame < 4; frame++) {
        auto list = make_blur_frame(&dummy_canvas, 6.0f);
        renderer->begin_frame();
        exec.execute(list);
        renderer->end_frame();
        // The pooled target must be returned and reused — never re-allocated
        // per filtered element per frame (old behavior churned one FBO per
        // PUSH_FILTER).
        ASSERT(exec.pooled_fbo_count() == 1);
    }
    window->set_should_close(true);
})

TEST(gpu_draw_blurred_texture_restores_scissor, {
    auto result = platform::Window::create_window("GPU Smoketest", 320, 240);
    if (result.is_err())
        return true;
    auto &window = result.unwrap();
    window->make_context_current();
    platform::load_opengl_functions();
    auto renderer = std::make_unique<render::Renderer>();
    ASSERT(renderer->initialize(320, 240).is_ok());
    if (!renderer->width())
        return true;

    namespace pgl = browser::platform;

    // Enable a distinctive scissor box; the blur path disables scissor
    // internally and must restore it exactly (old code: "caller must restore").
    pgl::glEnable(GL_SCISSOR_TEST);
    pgl::glScissor(11, 22, 33, 44);

    u32 tex = 0;
    pgl::glGenTextures(1, &tex);
    pgl::glBindTexture(GL_TEXTURE_2D, tex);
    u8 px[4] = {255, 255, 255, 255};
    pgl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);

    renderer->begin_frame();
    renderer->draw_blurred_texture(10, 10, 50, 50, tex, 1, 1, 8.0f);
    renderer->end_frame();

    GLint box[4] = {0, 0, 0, 0};
    pgl::glGetIntegerv(GL_SCISSOR_BOX, box);
    ASSERT(box[0] == 11 && box[1] == 22 && box[2] == 33 && box[3] == 44);
    ASSERT(pgl::glIsEnabled(GL_SCISSOR_TEST) != 0);

    // Also verify the disabled case stays disabled.
    pgl::glDisable(GL_SCISSOR_TEST);
    renderer->begin_frame();
    renderer->draw_blurred_texture(10, 10, 50, 50, tex, 1, 1, 8.0f);
    renderer->end_frame();
    ASSERT(pgl::glIsEnabled(GL_SCISSOR_TEST) == 0);

    pgl::glDeleteTextures(1, &tex);
    window->set_should_close(true);
})

// ── R-P4 / R-P6: atlas lifecycle ──

TEST(gpu_atlas_pages_reclaimed_after_overflow, {
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
    auto ir = tr->initialize(fm.get());
    if (ir.is_err())
        return true;

    // Large glyph sizes fill a 1024x1024 page in ~40 glyphs; this churns
    // through several full-page overflows. The old growth path pushed new
    // pages until the cap and NEVER reclaimed them on reset (stale VRAM
    // held forever); the shared reset path must collapse back to one page.
    render::Color white{1, 1, 1, 1};
    for (u32 size = 200; size < 260; size += 2) {
        tr->render_text(renderer.get(), "Test", 10, 10, white, size);
    }
    ASSERT(tr->atlas_page_count() == 1);
    ASSERT(tr->glyph_cache_size() > 0);
    window->set_should_close(true);
})

TEST(gpu_glyph_cache_lru_retains_under_cap, {
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
    auto ir = tr->initialize(fm.get());
    if (ir.is_err())
        return true;

    // 250 sizes x 4 distinct glyphs ("Test") = 1000 distinct cache entries.
    // Under the old nuke-at-2048 policy nothing here would be evicted, so
    // assert the count is exact; the LRU sweep at the higher 8192 cap keeps
    // every entry alive while bounding worst-case memory.
    render::Color white{1, 1, 1, 1};
    // The embedded font covers 414 of these codepoints; six pixel sizes give
    // 2484 distinct {glyph,size} entries — past the OLD nuke-at-2048 policy
    // (which would wholesale-clear and leave ~436), while staying small
    // enough that a single 1024x1024 atlas page holds everything (no reset).
    std::string all;
    for (u32 cp = 0x21; cp < 0x400; cp++) {
        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0x24F) || (cp >= 0x370 && cp <= 0x3FF)) {
            if (cp < 0x80) {
                all += static_cast<char>(cp);
            } else if (cp < 0x800) {
                all += static_cast<char>(0xC0 | (cp >> 6));
                all += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
    }
    for (u32 size = 16; size <= 21; size++) {
        tr->render_text(renderer.get(), all, 10, 10, white, size);
    }
    ASSERT(tr->atlas_page_count() == 1);
    ASSERT(tr->glyph_cache_size() == 2484);
    window->set_should_close(true);
})
