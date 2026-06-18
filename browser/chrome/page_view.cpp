#include "../../css/layout.hpp"
#include "../../html/traversal.hpp"
#include "../../render/paint_executor.hpp"
#include <functional>
#include "../../render/rasterizer.hpp"
#include "../settings.hpp"
#include "window.hpp"

namespace browser {

    void BrowserWindow::render_page() {
        // Check for loaded pages
        if (page_loader_) {
            auto loaded = page_loader_->try_get_loaded_page();
            if (loaded.has_value()) {
                current_page_ = std::move(loaded.value());
            }
        }
        if (!current_page_.has_value())
            return;

        auto &page = current_page_.value();
        f32 content_h = static_cast<f32>(viewport_height_) - chrome_height();
        f32 content_y = chrome_height();

        i32 page_h = 0;
        if (page.layout)
            page_h = static_cast<i32>(page.layout->content.height);
        chrome_.scroll_max = std::max(0, page_h - static_cast<i32>(content_h));
        chrome_.scroll_y = std::max(0, std::min(chrome_.scroll_max, chrome_.scroll_y));

        f32 sb_w = 10.0f;
        chrome_.rects.scrollbar = {static_cast<f32>(viewport_width_) - sb_w, content_y, sb_w, content_h};

        namespace pgl = browser::platform;
        render::PaintExecutor executor(renderer_.get(), text_renderer_.get());
        executor.set_offset(0, content_y - static_cast<f32>(chrome_.scroll_y));
        executor.set_base_clip(0, content_y, static_cast<f32>(viewport_width_), content_h);
        executor.set_image_data(page.images);
        renderer_->fill_rect(0, content_y, static_cast<f32>(viewport_width_), content_h, theme_.page_bg);
        renderer_->flush();
        if (page.display_list)
            executor.execute(*page.display_list);
        renderer_->flush();

        // Text selection highlight
        if (selection_.has_selection() && page.layout) {
            render::Color sel_color = {0.2f, 0.4f, 0.9f, 0.45f};
            bool drawing = false;
            std::function<void(const css::LayoutNode *, f32, f32)> draw_sel =
                [&](const css::LayoutNode *node, f32 ox, f32 oy) {
                    f32 nx = ox + node->content.x + node->padding.left + node->border.left;
                    f32 ny = oy + node->content.y + node->padding.top + node->border.top;
                    if (node == selection_.start_node) drawing = true;
                    if (drawing && node->is_text() && !node->text().empty()) {
                        f32 abs_x = nx;
                        f32 abs_y = ny - static_cast<f32>(chrome_.scroll_y) + content_y;
                        renderer_->fill_rect(abs_x, abs_y, node->content.width, node->content.height, sel_color);
                    }
                    for (auto &ch : node->children)
                        draw_sel(ch.get(), nx, ny);
                    if (node == selection_.end_node) drawing = false;
                };
            draw_sel(page.layout.get(), 0, 0);
        }

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

    void BrowserWindow::render_download_panel() {
        auto &t = theme_;
        f32 ox = 40, oy = chrome_height() + 20;
        f32 ow = static_cast<f32>(viewport_width_) - 80;
        f32 oh = static_cast<f32>(viewport_height_) - chrome_height() - 60;

        renderer_->fill_rect(ox, oy, ow, oh, t.surface);
        renderer_->stroke_rect(ox, oy, ow, oh, t.border, 1.0f);

        text_renderer_->render_text(renderer_.get(), "Downloads", ox + 16, oy + 12, t.text, 18);
        f32 iy = oy + 48;

        const auto &items = download_manager_->items();
        if (items.empty()) {
            text_renderer_->render_text(renderer_.get(), "No downloads", ox + 16, iy, t.text_secondary, 13);
        } else {
            for (size_t i = 0; i < items.size() && i < 10; i++) {
                auto &item = items[i];
                std::string dl_text = item.filename;
                if (item.completed)
                    dl_text += " (completed)";
                else if (item.cancelled)
                    dl_text += " (cancelled)";
                else if (!item.error.empty())
                    dl_text += " (" + item.error + ")";
                text_renderer_->render_text(renderer_.get(), dl_text, ox + 16, iy, t.text, 13);
                iy += 22;
            }
        }

        text_renderer_->render_text(renderer_.get(), "Press Esc to close", ox + 16, oy + oh - 30, t.text_secondary, 11);
    }

    void BrowserWindow::render_find_bar() {
        if (!chrome_.find_state.visible)
            return;
        auto &t = theme_;
        f32 bar_y = chrome_height();
        f32 bar_h = 30.0f;
        f32 bar_w = static_cast<f32>(viewport_width_);

        // Dark bar below toolbar
        renderer_->fill_rect(0, bar_y, bar_w, bar_h, {0.15f, 0.15f, 0.16f, 1.0f});
        renderer_->draw_line(0, bar_y, bar_w, bar_y, t.border, 1.0f);

        f32 tx = 10.0f;
        text_renderer_->render_text(renderer_.get(), "Find:", tx, bar_y + 7, {1.0f, 1.0f, 1.0f, 1.0f}, 13);
        tx += 40.0f;

        // Input area
        f32 input_w = 200.0f;
        renderer_->stroke_rect(tx, bar_y + 4, input_w, 22, {0.4f, 0.4f, 0.4f, 1.0f}, 1.0f);
        renderer_->fill_rect(tx + 1, bar_y + 5, input_w - 2, 20, {0.2f, 0.2f, 0.22f, 1.0f});

        std::string display_text = chrome_.find_state.query;
        if (display_text.empty())
            display_text = " ";
        text_renderer_->render_text(renderer_.get(), display_text, tx + 4, bar_y + 5, {1.0f, 1.0f, 1.0f, 1.0f}, 13);
        tx += input_w + 8;

        // Match count
        char count_text[32];
        u32 total = static_cast<u32>(chrome_.find_state.matches.size());
        u32 cur = total > 0 ? chrome_.find_state.current_match + 1 : 0;
        snprintf(count_text, sizeof(count_text), "%u/%u", cur, total);
        text_renderer_->render_text(renderer_.get(), count_text, tx, bar_y + 7, {0.8f, 0.8f, 0.8f, 1.0f}, 12);
        tx += 60.0f;

        // Prev/Next buttons
        text_renderer_->render_text(renderer_.get(), "[<]", tx, bar_y + 7, {0.9f, 0.9f, 0.9f, 1.0f}, 13);
        tx += 28.0f;
        text_renderer_->render_text(renderer_.get(), "[>]", tx, bar_y + 7, {0.9f, 0.9f, 0.9f, 1.0f}, 13);
    }

    void BrowserWindow::render_devtools() {
        auto &dt = chrome_.devtools;
        f32 devtools_h = 300.0f;
        f32 devtools_y = static_cast<f32>(viewport_height_) - devtools_h;
        f32 devtools_w = static_cast<f32>(viewport_width_);

        // Background
        renderer_->fill_rect(0, devtools_y, devtools_w, devtools_h, {0.12f, 0.12f, 0.14f, 1.0f});
        renderer_->draw_line(0, devtools_y, devtools_w, devtools_y, {0.3f, 0.3f, 0.32f, 1.0f}, 1.0f);

        // Tab bar
        f32 tab_x = 0;
        const char *tabs[] = {"Console", "Elements", "Network"};
        for (int i = 0; i < 3; i++) {
            bool active = (static_cast<int>(dt.active_tab) == i);
            if (active) {
                renderer_->fill_rect(tab_x, devtools_y, 100, 24, {0.2f, 0.2f, 0.22f, 1.0f});
            }
            text_renderer_->render_text(renderer_.get(),
                                        tabs[i],
                                        tab_x + 10,
                                        devtools_y + 5,
                                        active ? render::Color{1, 1, 1, 1} : render::Color{0.7f, 0.7f, 0.7f, 1.0f},
                                        12);
            tab_x += 100;
        }

        // Content area
        f32 content_y = devtools_y + 28;

        if (dt.active_tab == DevToolsState::CONSOLE) {
            // Console log entries
            f32 log_y = content_y;
            for (auto &entry : dt.console_entries) {
                if (log_y > devtools_y + devtools_h - 30)
                    break;
                render::Color c = {0.9f, 0.9f, 0.9f, 1.0f};
                if (entry.level == DevToolsState::ConsoleEntry::LEVEL_ERROR)
                    c = {1.0f, 0.3f, 0.3f, 1.0f};
                else if (entry.level == DevToolsState::ConsoleEntry::WARN)
                    c = {1.0f, 0.8f, 0.2f, 1.0f};
                text_renderer_->render_text(renderer_.get(), entry.text, 10, log_y, c, 11);
                log_y += 16;
            }
            // Input box
            f32 input_y = devtools_y + devtools_h - 24;
            renderer_->fill_rect(0, input_y, devtools_w, 24, {0.08f, 0.08f, 0.1f, 1.0f});
            text_renderer_->render_text(
                renderer_.get(), "> " + dt.console_input, 6, input_y + 4, render::Color{1, 1, 1, 1}, 12);
        } else if (dt.active_tab == DevToolsState::ELEMENTS) {
            // Show DOM tree as indented text, one line at a time
            if (current_page_.has_value() && current_page_->dom) {
                f32 line_y = content_y;
                html::Node *root = current_page_->dom.get();
                auto emit_line = [&](const std::string &s) {
                    if (line_y > devtools_y + devtools_h - 20)
                        return;
                    text_renderer_->render_text(
                        renderer_.get(), s, 10, line_y, render::Color{0.6f, 0.8f, 0.6f, 1.0f}, 11);
                    line_y += 14;
                };
                emit_line("<!DOCTYPE html>");
                html::traverse_depth_first(root, [&](html::Node *n) {
                    int depth = 0;
                    html::Node *p = n->parent;
                    while (p) {
                        depth++;
                        p = p->parent;
                    }
                    std::string indent(depth * 2, ' ');
                    if (n->type == html::NodeType::ELEMENT) {
                        auto *el = static_cast<html::Element *>(n);
                        std::string line = indent + "<" + el->tag_name;
                        if (!el->id().empty())
                            line += " id=\"" + el->id() + "\"";
                        line += ">";
                        emit_line(line);
                    } else if (n->type == html::NodeType::TEXT) {
                        auto *txt = static_cast<html::Text *>(n);
                        std::string t = txt->data;
                        if (t.length() > 80)
                            t = t.substr(0, 80) + "...";
                        // Escape newlines in text content
                        for (auto &c : t)
                            if (c == '\n')
                                c = ' ';
                        emit_line(indent + t);
                    }
                });
            } else {
                text_renderer_->render_text(
                    renderer_.get(), "No page loaded", 10, content_y, render::Color{0.7f, 0.7f, 0.7f, 1.0f}, 12);
            }
        } else if (dt.active_tab == DevToolsState::NETWORK) {
            // Network entries
            f32 ny = content_y;
            for (auto &entry : dt.network_entries) {
                if (ny > devtools_y + devtools_h - 20)
                    break;
                char net_text[256];
                snprintf(net_text,
                         sizeof(net_text),
                         "%s %s [%u] %ums %lluB",
                         entry.method.c_str(),
                         entry.url.c_str(),
                         entry.status,
                         (unsigned)entry.duration_ms,
                         (unsigned long long)entry.size);
                text_renderer_->render_text(renderer_.get(), net_text, 10, ny, render::Color{1, 1, 1, 1}, 11);
                ny += 16;
            }
        }
    }

    void BrowserWindow::render_settings() {
        auto &t = theme_;
        f32 ox = 40, oy = chrome_height() + 20;
        f32 ow = static_cast<f32>(viewport_width_) - 80;
        f32 oh = static_cast<f32>(viewport_height_) - chrome_height() - 60;

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
