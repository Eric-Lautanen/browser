#include "../../render/paint_executor.hpp"
#include "window.hpp"

namespace browser {

    void BrowserWindow::render_page() {
        if (page_loader_) {
            auto loaded = page_loader_->try_get_loaded_page();
            if (loaded.has_value()) {
                current_page_ = std::move(loaded.value());
            }
        }
        if (!current_page_.has_value())
            return;
        auto &page = current_page_.value();
        f32 content_h = static_cast<f32>(viewport_height_) - ChromeUI::CHROME_H;

        i32 page_h = 0;
        if (page.layout)
            page_h = static_cast<i32>(page.layout->content.height);
        chrome_.scroll_max = std::max(0, page_h - static_cast<i32>(content_h));
        chrome_.scroll_y = std::max(0, std::min(chrome_.scroll_max, chrome_.scroll_y));

        f32 sb_w = 10.0f;
        chrome_.rects.scrollbar = {static_cast<f32>(viewport_width_) - sb_w, ChromeUI::CHROME_H, sb_w, content_h};

        namespace pgl = browser::platform;
        pgl::glEnable(GL_SCISSOR_TEST);
        pgl::glScissor(0, 0, static_cast<GLsizei>(viewport_width_), static_cast<GLsizei>(content_h));
        renderer_->fill_rect(0, ChromeUI::CHROME_H, static_cast<f32>(viewport_width_), content_h, theme_.page_bg);
        renderer_->flush();
        render::PaintExecutor executor(renderer_.get(), text_renderer_.get());
        executor.set_offset(0, ChromeUI::CHROME_H - static_cast<f32>(chrome_.scroll_y));
        if (page.display_list)
            executor.execute(*page.display_list);
        pgl::glEnable(GL_SCISSOR_TEST);
        pgl::glScissor(0, 0, static_cast<GLsizei>(viewport_width_), static_cast<GLsizei>(content_h));
        renderer_->flush();
        pgl::glDisable(GL_SCISSOR_TEST);

        render_scrollbar();
    }

    void BrowserWindow::render_scrollbar() {
        if (chrome_.scroll_max <= 0)
            return;
        auto &sb = chrome_.rects.scrollbar;
        f32 content_h = sb.h;
        renderer_->fill_rect(sb.x, sb.y, sb.w, sb.h, {0.85f, 0.85f, 0.85f, 1.0f});
        f32 thumb_h = std::max(20.0f, sb.h * (content_h / (content_h + chrome_.scroll_max)));
        f32 thumb_y = sb.y + (static_cast<f32>(chrome_.scroll_y) / std::max(1, chrome_.scroll_max)) * (sb.h - thumb_h);
        renderer_->fill_rect(sb.x, thumb_y, sb.w, thumb_h, {0.5f, 0.5f, 0.5f, 1.0f});
    }

    void BrowserWindow::render_settings() {
        auto &t = theme_;
        f32 ox = 40, oy = ChromeUI::CHROME_H + 20;
        f32 ow = static_cast<f32>(viewport_width_) - 80;
        f32 oh = static_cast<f32>(viewport_height_) - ChromeUI::CHROME_H - 60;

        renderer_->fill_rect(ox, oy, ow, oh, t.surface);
        renderer_->stroke_rect(ox, oy, ow, oh, t.border, 1.0f);

        text_renderer_->render_text(renderer_.get(), "Settings", ox + 16, oy + 12, t.text, 18);
        f32 iy = oy + 48;

        text_renderer_->render_text(renderer_.get(), "Theme", ox + 16, iy, t.text, 13);
        renderer_->fill_rect(ox + 155, iy - 2, 80, 22, t.surface_hover);
        const char *label = (Theme::current == ThemeMode::LIGHT) ? "Light" : "Dark";
        text_renderer_->render_text(renderer_.get(), label, ox + 165, iy, t.accent, 13);
        renderer_->draw_line(ox + 12, iy + 22, ox + ow - 12, iy + 22, t.border, 0.5f);

        text_renderer_->render_text(renderer_.get(), "Press Esc to close", ox + 16, oy + oh - 30, t.text_secondary, 11);
    }

}  // namespace browser
