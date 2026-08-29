#include "../../html/form_state.hpp"
#include "../../html/form_submission.hpp"
#include "../../html/hit_test.hpp"
#include "../../html/traversal.hpp"
#include "../../net/url.hpp"
#include "../../platform/window_win32.hpp"
#include "../bookmarks.hpp"
#include "../paths.hpp"
#include "../settings.hpp"
#include "dialogs.hpp"
#include "window.hpp"

#include <commctrl.h>
#include <functional>
#include <windows.h>

namespace browser {

    namespace {

        // Helper: check if a layout node or any ancestor has user-select: none
        static bool has_user_select_none(const css::LayoutNode *node) {
            while (node) {
                auto *us = node->style().get("user-select");
                if (us && us->type == css::CSSValue::Type::KEYWORD && us->keyword == "none")
                    return true;
                node = node->parent;
            }
            return false;
        }

        // Helper: adjust a number input's value by +step or -step, clamping to min/max
        static void adjust_number_value(html::Element *el, f32 delta) {
            std::string step_str = el->get_attribute("step");
            f32 step = step_str.empty() ? 1.0f : std::strtof(step_str.c_str(), nullptr);
            if (step <= 0)
                step = 1.0f;

            std::string val = html::g_form_state.get_value(el);
            f32 cur = std::strtof(val.c_str(), nullptr);
            cur += delta * step;

            std::string min_str = el->get_attribute("min");
            if (!min_str.empty()) {
                f32 min_val = std::strtof(min_str.c_str(), nullptr);
                if (cur < min_val)
                    cur = min_val;
            }
            std::string max_str = el->get_attribute("max");
            if (!max_str.empty()) {
                f32 max_val = std::strtof(max_str.c_str(), nullptr);
                if (cur > max_val)
                    cur = max_val;
            }

            char buf[64];
            snprintf(buf, sizeof(buf), "%g", cur);
            html::g_form_state.set_value(el, buf);
            html::g_form_state.caret_position = static_cast<u32>(std::string(buf).size());
        }

        char keycode_to_char(platform::KeyCode key, bool shifted) {
            int base = static_cast<int>(platform::KeyCode::A);
            if (key >= platform::KeyCode::A && key <= platform::KeyCode::Z) {
                if (shifted)
                    return 'A' + (static_cast<int>(key) - base);
                return 'a' + (static_cast<int>(key) - base);
            }
            int num_base = static_cast<int>(platform::KeyCode::_0);
            if (key >= platform::KeyCode::_0 && key <= platform::KeyCode::_9) {
                if (shifted) {
                    const char shifted_digits[] = ")!@#$%^&*(";
                    return shifted_digits[static_cast<int>(key) - num_base];
                }
                return '0' + (static_cast<int>(key) - num_base);
            }
            if (key == platform::KeyCode::SPACE)
                return ' ';
            if (key == platform::KeyCode::PERIOD)
                return shifted ? '>' : '.';
            if (key == platform::KeyCode::COMMA)
                return shifted ? '<' : ',';
            if (key == platform::KeyCode::SLASH)
                return shifted ? '?' : '/';
            if (key == platform::KeyCode::SEMICOLON)
                return shifted ? ':' : ';';
            if (key == platform::KeyCode::QUOTE)
                return shifted ? '"' : '\'';
            if (key == platform::KeyCode::BACKSLASH)
                return shifted ? '|' : '\\';
            if (key == platform::KeyCode::MINUS)
                return shifted ? '_' : '-';
            if (key == platform::KeyCode::EQUALS)
                return shifted ? '+' : '=';
            if (key == platform::KeyCode::LBRACKET)
                return shifted ? '{' : '[';
            if (key == platform::KeyCode::RBRACKET)
                return shifted ? '}' : ']';
            if (key == platform::KeyCode::BACKTICK)
                return shifted ? '~' : '`';
            return '\0';
        }

        void clipboard_copy(const std::string &text) {
            if (text.empty())
                return;
            int wide_len = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
            if (wide_len <= 0)
                return;
            HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, (wide_len + 1) * sizeof(wchar_t));
            if (!hglb)
                return;
            wchar_t *buf = (wchar_t *)GlobalLock(hglb);
            if (buf) {
                MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), buf, wide_len);
                buf[wide_len] = 0;
            }
            GlobalUnlock(hglb);
            if (OpenClipboard(nullptr)) {
                EmptyClipboard();
                SetClipboardData(CF_UNICODETEXT, hglb);
                CloseClipboard();
            } else {
                GlobalFree(hglb);
            }
        }

        std::string clipboard_paste() {
            std::string result;
            if (!OpenClipboard(nullptr))
                return result;
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (h) {
                wchar_t *wtext = (wchar_t *)GlobalLock(h);
                if (wtext) {
                    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, nullptr, 0, nullptr, nullptr);
                    if (utf8_len > 1) {
                        result.resize(static_cast<size_t>(utf8_len - 1));
                        WideCharToMultiByte(CP_UTF8, 0, wtext, -1, &result[0], utf8_len, nullptr, nullptr);
                    }
                    GlobalUnlock(h);
                }
            } else {
                // Fallback: try CF_TEXT and convert from ANSI
                h = GetClipboardData(CF_TEXT);
                if (h) {
                    char *text = (char *)GlobalLock(h);
                    if (text) {
                        int wide_len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
                        if (wide_len > 0) {
                            std::vector<wchar_t> wbuf(static_cast<size_t>(wide_len));
                            MultiByteToWideChar(CP_ACP, 0, text, -1, wbuf.data(), wide_len);
                            int utf8_len =
                                WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, nullptr, 0, nullptr, nullptr);
                            if (utf8_len > 1) {
                                result.resize(static_cast<size_t>(utf8_len - 1));
                                WideCharToMultiByte(
                                    CP_UTF8, 0, wbuf.data(), -1, &result[0], utf8_len, nullptr, nullptr);
                            }
                        } else {
                            result = text;
                        }
                    }
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            return result;
        }

        // Resolve a link href against the current page URL. Returns "" for
        // empty/fragment-only hrefs (those are same-page anchors).
        std::string resolve_link_url(const std::string &href, const std::string &base) {
            if (href.empty() || href[0] == '#')
                return "";
            std::string resolved = href;
            if (href.find("://") == std::string::npos) {
                auto base_parsed = net::URL::parse(base);
                if (base_parsed.is_ok()) {
                    auto resolved_r = base_parsed.unwrap().resolve(href);
                    if (resolved_r.is_ok())
                        resolved = resolved_r.unwrap().to_string();
                }
            }
            return resolved;
        }

        // Word character for address-bar double-click selection. Deliberately
        // ASCII-only: multi-byte UTF-8 sequences act as boundaries, which keeps
        // the byte-indexed selection from splitting a code point.
        bool addr_word_char(unsigned char c) {
            return std::isalnum(c) != 0 || c == '_';
        }

    }  // namespace

    void BrowserWindow::handle_event(const platform::Event &e) {
        // BR-P2: any input may change what is on screen; the render gate
        // picks this up on the next loop iteration.
        frame_dirty_ = true;
        if (e.type == platform::Event::Type::KEY_DOWN) {
            if (e.key == platform::KeyCode::CTRL || e.key == platform::KeyCode::LCTRL ||
                e.key == platform::KeyCode::RCTRL)
                chrome_.ctrl_down = true;
            else if (e.key == platform::KeyCode::SHIFT || e.key == platform::KeyCode::LSHIFT ||
                     e.key == platform::KeyCode::RSHIFT)
                chrome_.shift_down = true;
            else if (e.key == platform::KeyCode::ALT || e.key == platform::KeyCode::LALT ||
                     e.key == platform::KeyCode::RALT)
                chrome_.alt_down = true;
            handle_key_down(e);
        } else if (e.type == platform::Event::Type::KEY_UP) {
            if (e.key == platform::KeyCode::CTRL || e.key == platform::KeyCode::LCTRL ||
                e.key == platform::KeyCode::RCTRL)
                chrome_.ctrl_down = false;
            else if (e.key == platform::KeyCode::SHIFT || e.key == platform::KeyCode::LSHIFT ||
                     e.key == platform::KeyCode::RSHIFT)
                chrome_.shift_down = false;
            else if (e.key == platform::KeyCode::ALT || e.key == platform::KeyCode::LALT ||
                     e.key == platform::KeyCode::RALT)
                chrome_.alt_down = false;
        } else if (e.type == platform::Event::Type::MOUSE_DOWN) {
            handle_mouse_click(e.mouse_x, e.mouse_y, e.button);
        } else if (e.type == platform::Event::Type::MOUSE_UP) {
            chrome_.scroll_dragging = false;
            if (chrome_.textarea_resize.active) {
                chrome_.textarea_resize.active = false;
                chrome_.textarea_resize.element = nullptr;
                chrome_.textarea_resize.layout_node = nullptr;
                // BR-P3: settle the final size with one last relayout.
                relayout_pending_ = true;
                frame_dirty_ = true;
            }
        } else if (e.type == platform::Event::Type::MOUSE_MOVE) {
            if (chrome_.scroll_dragging) {
                f32 sb_h = chrome_.rects.scrollbar.h;
                f32 thumb_h = std::max(20.0f, sb_h * sb_h / (sb_h + chrome_.scroll_max));
                f32 range = sb_h - thumb_h;
                if (range > 0) {
                    f32 delta = static_cast<f32>(e.mouse_y - chrome_.scroll_drag_start_y);
                    chrome_.scroll_y =
                        static_cast<i32>(chrome_.scroll_drag_start_pos + delta * chrome_.scroll_max / range);
                    chrome_.scroll_y = std::max(0, std::min(chrome_.scroll_max, chrome_.scroll_y));
                }
            }
            handle_mouse_move(e.mouse_x, e.mouse_y);
        } else if (e.type == platform::Event::Type::MOUSE_SCROLL) {
            handle_scroll(e.scroll_delta);
        } else if (e.type == platform::Event::Type::WINDOW_RESIZE) {
            viewport_width_ = e.width;
            viewport_height_ = e.height;
            renderer_->set_viewport(e.width, e.height);
            compute_layout();
            if (page_loader_)
                page_loader_->set_viewport_size(viewport_width_, viewport_height_);
            // During live drag-resize, defer full re-layout for 100ms
            resize_pending_ = true;
            resize_last_time_ = std::chrono::steady_clock::now();
        }
    }

    void BrowserWindow::handle_mouse_click(i32 mx, i32 my, platform::MouseButton button) {
        if (button == platform::MouseButton::RIGHT) {
            // Right-click opens the page context menu: link actions on a
            // link, navigation actions elsewhere. Any open popup closes first.
            close_popups();
            if (my > chrome_height()) {
                chrome_.show_context_menu = true;
                chrome_.context_menu_x = static_cast<f32>(mx);
                chrome_.context_menu_y = static_cast<f32>(my);
                chrome_.hovered_context_item = -1;
                chrome_.context_link_url.clear();
                chrome_.context_on_link = false;
                if (current_page_.has_value() && current_page_->layout) {
                    f32 py = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                    auto ht = html::hit_test(current_page_->layout.get(), static_cast<f32>(mx), py);
                    if (ht.element && ht.element->tag_name == "a") {
                        std::string url = resolve_link_url(ht.element->get_attribute("href"), chrome_.url);
                        if (!url.empty()) {
                            chrome_.context_on_link = true;
                            chrome_.context_link_url = url;
                        }
                    }
                }
            }
            return;
        }
        if (button == platform::MouseButton::MIDDLE) {
            // Middle-click on a tab closes it; middle-click on a link opens
            // it in a new background tab. Open popups get out of the way.
            close_popups();
            i32 tab = tab_index_at(mx, my);
            if (tab >= 0) {
                close_tab(static_cast<u32>(tab));
                return;
            }
            if (my > chrome_height() && current_page_.has_value() && current_page_->layout) {
                f32 py = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                auto ht = html::hit_test(current_page_->layout.get(), static_cast<f32>(mx), py);
                if (ht.element && ht.element->tag_name == "a") {
                    std::string url = resolve_link_url(ht.element->get_attribute("href"), chrome_.url);
                    if (!url.empty()) {
                        open_background_tab(url);
                        return;
                    }
                }
            }
            return;
        }

        // A left-click anywhere above the page area while a popup is open is
        // consumed by popup dismissal — never by the toolbar underneath.
        if (popup_open() && my <= chrome_height()) {
            close_popups();
            return;
        }

        if (my > chrome_height()) {
            // Popup dispatch — an open popup owns the click: it either
            // activates an item or is dismissed; the click never reaches
            // the page or panels underneath.
            if (chrome_.show_context_menu) {
                auto r = context_menu_rect();
                if (is_in_rect(mx, my, r)) {
                    constexpr f32 PAD = 4.0f;
                    i32 idx = static_cast<i32>((static_cast<f32>(my) - r.y - PAD) / ChromeUI::CTX_ITEM_H);
                    if (chrome_.context_on_link) {
                        if (idx == 0 && !chrome_.context_link_url.empty())
                            open_background_tab(chrome_.context_link_url);
                        else if (idx == 1 && !chrome_.context_link_url.empty())
                            clipboard_copy(chrome_.context_link_url);
                    } else if (idx == 0) {
                        navigate_back();
                    } else if (idx == 1) {
                        navigate_forward();
                    } else if (idx == 2) {
                        refresh();
                    }
                }
                chrome_.show_context_menu = false;
                chrome_.hovered_context_item = -1;
                return;
            }
            if (chrome_.show_menu) {
                auto geom = ChromeUI::menu_geometry(static_cast<f32>(viewport_width_),
                                                    static_cast<f32>(viewport_height_),
                                                    chrome_height(),
                                                    chrome_.rects.menu);
                if (mx >= geom.x && mx <= geom.x + geom.w && my >= geom.y && my <= geom.y + geom.h) {
                    constexpr f32 PAD = 4.0f;
                    f32 rel_y = static_cast<f32>(my) - geom.y - PAD;
                    i32 idx = static_cast<i32>(rel_y / ChromeUI::MENU_ITEM_H);
                    if (idx >= 0 && idx < static_cast<i32>(ChromeUI::MENU_ITEM_COUNT)) {
                        switch (idx) {
                            case 0:
                                new_tab();
                                break;
                            case 1:
                                handle_bookmark_click();
                                break;
                            case 2:
                                chrome_.find_state.show();
                                break;
                            case 3:
                                chrome_.show_downloads = true;
                                break;
                            case 4:
                                chrome_.show_settings = true;
                                break;
                        }
                    }
                }
                chrome_.show_menu = false;
                chrome_.hovered_menu_item = -1;
                return;
            }
            if (chrome_.show_bookmarks_dropdown) {
                auto dd = bookmarks_dropdown_rect();
                if (is_in_rect(mx, my, dd)) {
                    auto all = bookmarks_ ? bookmarks_->all() : std::vector<Bookmark>();
                    constexpr f32 ITEM_H = 28.0f, HEADER_H = 28.0f, PAD = 4.0f;
                    constexpr f32 DEL_W = 22.0f;
                    f32 rel_y = static_cast<f32>(my) - dd.y - PAD - HEADER_H;
                    if (rel_y >= 0.0f) {
                        // Account for scroll offset
                        f32 scrolled_y = rel_y + chrome_.bookmark_scroll_offset;
                        i32 idx = static_cast<i32>(scrolled_y / ITEM_H);
                        if (idx >= 0 && idx < static_cast<i32>(all.size())) {
                            if (static_cast<f32>(mx) >= dd.x + dd.w - DEL_W) {
                                // "×" on the right deletes the bookmark; the
                                // dropdown stays open for more cleanup
                                if (bookmarks_) {
                                    bookmarks_->remove(all[static_cast<size_t>(idx)].url);
                                    bookmarks_->save_to_file(BookmarkManager::default_path());
                                    chrome_.is_bookmarked = bookmarks_->is_bookmarked(chrome_.url);
                                }
                            } else {
                                navigate(all[static_cast<size_t>(idx)].url);
                                chrome_.show_bookmarks_dropdown = false;
                            }
                        }
                    }
                    chrome_.hovered_bookmark_item = -1;
                    chrome_.hovered_bookmark_delete = -1;
                    chrome_.bookmark_scroll_offset = 0.0f;
                    return;
                }
                chrome_.show_bookmarks_dropdown = false;
                chrome_.hovered_bookmark_item = -1;
                chrome_.hovered_bookmark_delete = -1;
                chrome_.bookmark_scroll_offset = 0.0f;
                return;
            }
            if (chrome_.show_settings) {
                auto panel = overlay_panel_rect();
                if (is_in_rect(mx, my, {panel.x + 155, panel.y + 46, 80, 22})) {
                    Theme::toggle();
                    set_theme(Theme::current);
                    if (settings_) {
                        settings_->set_theme(Theme::current);
                        settings_->save_to_file(data_dir() + "/settings.txt");
                    }
                    chrome_.address_focused = false;
                    return;
                }
                // Clicking outside the panel dismisses it; inside, it stays
                if (!is_in_rect(mx, my, panel))
                    chrome_.show_settings = false;
                return;
            }
            if (chrome_.show_downloads) {
                if (!is_in_rect(mx, my, overlay_panel_rect()))
                    chrome_.show_downloads = false;
                return;
            }

            if (chrome_.devtools.visible) {
                // DevTools tab bar click handling
                f32 dt_y = static_cast<f32>(viewport_height_) - 300.0f;
                if (my >= dt_y + 1 && my <= dt_y + 24) {
                    f32 tx = mx;
                    for (int i = 0; i < 3; i++) {
                        if (tx >= i * 100 && tx < (i + 1) * 100) {
                            chrome_.devtools.active_tab = static_cast<DevToolsState::Tab>(i);
                            break;
                        }
                    }
                }
                return;
            }
            if (chrome_.find_state.visible) {
                f32 bar_y = chrome_height();
                f32 bar_h = 30.0f;
                if (my >= bar_y && my < bar_y + bar_h) {
                    // Find bar buttons: [<] at tx≈290, [>] at tx≈318
                    f32 tx = 10.0f + 40.0f + 200.0f + 60.0f;
                    if (mx >= tx && mx < tx + 28) {
                        // [<] previous
                        chrome_.find_state.previous();
                    } else if (mx >= tx + 28 && mx < tx + 56) {
                        // [>] next
                        chrome_.find_state.next();
                    }
                }
                return;
            }

            if (chrome_.scroll_max > 0 && is_in_rect(mx, my, chrome_.rects.scrollbar)) {
                chrome_.scroll_dragging = true;
                chrome_.scroll_drag_start_y = my;
                chrome_.scroll_drag_start_pos = chrome_.scroll_y;
                chrome_.address_focused = false;
                return;
            }

            chrome_.address_focused = false;

            // Handle multi-select list box clicks (always visible)
            if (current_page_.has_value()) {
                f32 py2 = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                auto &dr2 = html::g_form_state.select_dropdown_rect;
                if (dr2.width > 0 && mx >= dr2.x && mx <= dr2.x + dr2.width && py2 >= dr2.y &&
                    py2 <= dr2.y + dr2.height) {
                    // Must have a focused multi-select element
                    auto *focused = html::g_form_state.focused_element;
                    if (focused && focused->tag_name == "select" && focused->has_attribute("multiple")) {
                        f32 rel_y = py2 - dr2.y;
                        int row = static_cast<int>(rel_y / 20.0f);
                        if (row >= 0) {
                            int target_option = 0;
                            int running_idx = 0;
                            bool found = false;
                            std::function<void(html::Node *, int &, int &)> walk2 =
                                [&](html::Node *parent, int &row_idx, int &opt_idx) {
                                    if (found)
                                        return;
                                    for (auto &c : parent->children) {
                                        if (found)
                                            return;
                                        if (c->type != html::NodeType::ELEMENT)
                                            continue;
                                        auto *ch = static_cast<html::Element *>(c.get());
                                        if (ch->tag_name == "option") {
                                            if (row_idx == row) {
                                                target_option = opt_idx;
                                                found = true;
                                                return;
                                            }
                                            row_idx++;
                                            opt_idx++;
                                        } else if (ch->tag_name == "optgroup") {
                                            if (row_idx == row) {
                                                found = true;
                                                return;
                                            }
                                            row_idx++;
                                            walk2(ch, row_idx, opt_idx);
                                        }
                                    }
                                };
                            walk2(const_cast<html::Element *>(focused), running_idx, target_option);
                            if (found) {
                                html::g_form_state.set_selected_index(const_cast<html::Element *>(focused),
                                                                      target_option);
                            }
                        }
                        return;
                    }
                }
            }

            // Handle open select dropdown clicks
            if (html::g_form_state.open_select && current_page_.has_value()) {
                f32 py = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                auto &dr = html::g_form_state.select_dropdown_rect;
                if (mx >= dr.x && mx <= dr.x + dr.width && py >= dr.y && py <= dr.y + dr.height) {
                    // Click is inside dropdown - find which visible row was clicked
                    f32 rel_y = py - dr.y;
                    int row = static_cast<int>(rel_y / 20.0f);
                    if (row >= 0) {
                        // Walk options including optgroups to find the nth selectable option
                        int target_option = 0;
                        int running_idx = 0;
                        bool found = false;
                        std::function<void(html::Node *, int &, int &)> walk =
                            [&](html::Node *parent, int &row_idx, int &opt_idx) {
                                if (found)
                                    return;
                                for (auto &c : parent->children) {
                                    if (found)
                                        return;
                                    if (c->type != html::NodeType::ELEMENT)
                                        continue;
                                    auto *ch = static_cast<html::Element *>(c.get());
                                    if (ch->tag_name == "option") {
                                        if (row_idx == row) {
                                            target_option = opt_idx;
                                            found = true;
                                            return;
                                        }
                                        row_idx++;
                                        opt_idx++;
                                    } else if (ch->tag_name == "optgroup") {
                                        // Optgroup header is a row too (non-selectable)
                                        if (row_idx == row) {
                                            found = true;
                                            return;
                                        }
                                        row_idx++;
                                        walk(ch, row_idx, opt_idx);
                                    }
                                }
                            };
                        walk(const_cast<html::Element *>(html::g_form_state.open_select), running_idx, target_option);
                        if (found) {
                            html::g_form_state.set_selected_index(
                                const_cast<html::Element *>(html::g_form_state.open_select), target_option);
                        }
                    }
                    html::g_form_state.close_select();
                    return;
                }
                // Click outside dropdown - close it
                html::g_form_state.close_select();
                // Don't return - let the click also do whatever else it would do
            }

            // Hit test against page content — do this FIRST for interactive elements
            if (current_page_.has_value() && current_page_->layout) {
                f32 py = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                auto ht = html::hit_test(current_page_->layout.get(), static_cast<f32>(mx), py);

                // Check for pointer-events: none — skip elements that don't accept pointer input
                if (ht.element && ht.layout_node) {
                    auto *pe = ht.layout_node->style().get("pointer-events");
                    if (pe && pe->type == css::CSSValue::Type::KEYWORD && pe->keyword == "none") {
                        ht.element = nullptr;
                        ht.layout_node = nullptr;
                    }
                }

                // If clicked on an interactive element, don't select text
                bool on_interactive = false;
                if (ht.element) {
                    std::string tag = ht.element->tag_name;
                    std::string type = ht.element->get_attribute("type");
                    on_interactive =
                        (tag == "a" || tag == "input" || tag == "button" || tag == "textarea" || tag == "select");
                }

                if (!on_interactive && current_page_->layout) {
                    // Walk tree to find text node at click position
                    f32 gx = static_cast<f32>(mx);
                    f32 gy = py;
                    const css::LayoutNode *text_node = nullptr;
                    std::function<void(const css::LayoutNode *, f32, f32)> find_text =
                        [&](const css::LayoutNode *node, f32 ox, f32 oy) {
                            f32 nx = ox + node->content.x + node->padding.left + node->border.left;
                            f32 ny = oy + node->content.y + node->padding.top + node->border.top;
                            if (node->is_text() && !node->text().empty() && !text_node) {
                                if (!has_user_select_none(node) && gx >= nx && gx <= nx + node->content.width &&
                                    gy >= ny && gy <= ny + node->content.height) {
                                    text_node = node;
                                }
                            }
                            for (auto &ch : node->children)
                                find_text(ch.get(), ox + node->content.x, oy + node->content.y);
                        };
                    find_text(current_page_->layout.get(), 0, 0);
                    if (text_node) {
                        selection_.clear();
                        selection_.active = true;
                        selection_.start_node = text_node;
                        selection_.end_node = text_node;
                        selection_.start_offset = 0;
                        selection_.end_offset = static_cast<u32>(text_node->text().size());
                        selection_.selected_text = text_node->text();
                        html::g_form_state.hovered_element = nullptr;
                        return;
                    }
                }

                // Clear selection when clicking on non-text area
                selection_.active = false;

                if (ht.element) {
                    // Skip disabled form controls
                    if (ht.element->has_attribute("disabled")) {
                        // Don't focus or interact with disabled elements
                    } else {
                        html::g_form_state.hovered_element = ht.element;
                        std::string tag = ht.element->tag_name;
                        std::string type = ht.element->get_attribute("type");

                        if (tag == "input" && (type.empty() || type == "text" || type == "password" ||
                                               type == "email" || type == "search" || type == "url")) {
                            html::g_form_state.focus(ht.element);
                        } else if (tag == "input" && type == "number") {
                            html::g_form_state.focus(ht.element);
                            // Check if click is on spin buttons
                            if (ht.layout_node) {
                                css::Rect box = ht.layout_node->get_border_box();
                                f32 spin_w = 18.0f;
                                f32 spin_x = box.x + box.width - spin_w;
                                if (static_cast<f32>(mx) >= spin_x && static_cast<f32>(mx) <= box.x + box.width) {
                                    f32 click_y =
                                        static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                                    f32 half_h = box.height / 2.0f;
                                    if (click_y < box.y + half_h) {
                                        // Spin up
                                        adjust_number_value(ht.element, 1);
                                    } else {
                                        // Spin down
                                        adjust_number_value(ht.element, -1);
                                    }
                                }
                            }
                        } else if (tag == "input" && type == "range") {
                            html::g_form_state.focus(ht.element);
                            // Set value based on click position
                            if (ht.layout_node) {
                                css::Rect box = ht.layout_node->get_border_box();
                                f32 min_val = 0, max_val = 100;
                                std::string min_str = ht.element->get_attribute("min");
                                std::string max_str = ht.element->get_attribute("max");
                                if (!min_str.empty())
                                    min_val = std::strtof(min_str.c_str(), nullptr);
                                if (!max_str.empty())
                                    max_val = std::strtof(max_str.c_str(), nullptr);
                                f32 frac = (static_cast<f32>(mx) - box.x) / box.width;
                                if (frac < 0)
                                    frac = 0;
                                if (frac > 1)
                                    frac = 1;
                                f32 val = min_val + (max_val - min_val) * frac;
                                char buf[64];
                                snprintf(buf, sizeof(buf), "%g", val);
                                html::g_form_state.set_value(ht.element, buf);
                            }
                        } else if (tag == "input" && type == "file") {
                            html::g_form_state.focus(ht.element);
                            std::string out;
                            if (show_file_picker(GetActiveWindow(), out))
                                html::g_form_state.set_value(ht.element, out);
                        } else if (tag == "input" && type == "color") {
                            html::g_form_state.focus(ht.element);
                            std::string out;
                            if (show_color_picker(GetActiveWindow(), html::g_form_state.get_value(ht.element), out))
                                html::g_form_state.set_value(ht.element, out);
                        } else if (tag == "input" && type == "checkbox") {
                            html::g_form_state.toggle_checkbox(ht.element);
                            html::g_form_state.focus(ht.element);
                        } else if (tag == "input" && type == "radio") {
                            html::g_form_state.set_checked(ht.element, true);
                            html::g_form_state.focus(ht.element);
                        } else if (tag == "input" && type == "submit") {
                            html::g_form_state.focus(ht.element);
                            {
                                std::string nav_url = html::handle_form_submission(ht.element);
                                if (!nav_url.empty())
                                    start_load(nav_url);
                            }
                        } else if (tag == "button") {
                            html::g_form_state.focus(ht.element);
                            std::string bt = ht.element->get_attribute("type");
                            if (bt.empty() || bt == "submit") {
                                std::string nav_url = html::handle_form_submission(ht.element);
                                if (!nav_url.empty())
                                    start_load(nav_url);
                            }
                        } else if (tag == "textarea") {
                            // Check if clicking on resize handle
                            std::string resize = "both";
                            if (ht.layout_node) {
                                auto *rs = ht.layout_node->style().get("resize");
                                if (rs && rs->type == css::CSSValue::Type::KEYWORD) {
                                    resize = rs->keyword;
                                }
                            }
                            if (resize != "none" && ht.layout_node) {
                                f32 handle_size = 12.0f;
                                f32 hx = ht.hit_rect.x + ht.hit_rect.width - handle_size;
                                f32 hy = ht.hit_rect.y + ht.hit_rect.height - handle_size;
                                f32 px = static_cast<f32>(mx);
                                f32 py = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                                if (px >= hx && px <= hx + handle_size && py >= hy && py <= hy + handle_size) {
                                    // Start resize drag
                                    chrome_.textarea_resize.active = true;
                                    chrome_.textarea_resize.element = ht.element;
                                    chrome_.textarea_resize.layout_node = ht.layout_node;
                                    chrome_.textarea_resize.start_mouse_x = px;
                                    chrome_.textarea_resize.start_mouse_y = py;
                                    chrome_.textarea_resize.start_width = ht.hit_rect.width;
                                    chrome_.textarea_resize.start_height = ht.hit_rect.height;
                                    return;
                                }
                            }
                            html::g_form_state.focus(ht.element);
                        } else if (tag == "select") {
                            html::g_form_state.focus(ht.element);
                            html::g_form_state.toggle_select(ht.element);
                        } else if (tag == "summary") {
                            // Toggle parent <details> open attribute
                            html::Node *p = ht.element->parent;
                            while (p) {
                                if (p->type == html::NodeType::ELEMENT) {
                                    auto *pe = static_cast<html::Element *>(p);
                                    if (pe->tag_name == "details") {
                                        if (pe->has_attribute("open"))
                                            pe->attributes.erase("open");
                                        else
                                            pe->attributes["open"] = "";
                                        renderer_->set_needs_redraw();
                                        break;
                                    }
                                }
                                p = p->parent;
                            }
                        } else if (tag == "label") {
                            std::string for_id = ht.element->get_attribute("for");
                            if (!for_id.empty() && current_page_ && current_page_->dom) {
                                html::Node *target = html::find_element_by_id(current_page_->dom.get(), for_id);
                                if (target && target->type == html::NodeType::ELEMENT) {
                                    html::g_form_state.focus(static_cast<html::Element *>(target));
                                }
                            }
                        } else if (tag == "a") {
                            std::string href = ht.element->get_attribute("href");
                            std::string url = resolve_link_url(href, chrome_.url);
                            if (!href.empty()) {
                                if (href[0] == '#') {
                                    // Scroll-to-anchor: update scroll position
                                    std::string target_id = href.substr(1);
                                    // Walk layout tree to find element with matching id
                                    if (current_page_->layout) {
                                        html::Node *found =
                                            html::find_element_by_id(current_page_->dom.get(), target_id);
                                        if (found && found->type == html::NodeType::ELEMENT) {
                                            auto *found_el = static_cast<html::Element *>(found);
                                            // Find this element in the styles map and compute its Y position
                                            // Simple approach: search layout tree for matching element
                                            struct FindResult {
                                                bool found;
                                                f32 y;
                                            };
                                            std::function<FindResult(html::Element *, css::LayoutNode *, f32)> find_y =
                                                [&](html::Element *target,
                                                    css::LayoutNode *node,
                                                    f32 acc_y) -> FindResult {
                                                if (node->node() == target)
                                                    return {true, acc_y + node->content.y};
                                                for (auto &child : node->children) {
                                                    auto r = find_y(target, child.get(), acc_y + node->content.y);
                                                    if (r.found)
                                                        return r;
                                                }
                                                return {false, 0};
                                            };
                                            auto fr = find_y(found_el, current_page_->layout.get(), 0);
                                            if (fr.found) {
                                                i32 target = static_cast<i32>(fr.y);
                                                // Check for scroll-behavior: smooth on root element
                                                bool smooth = false;
                                                auto *sb = current_page_->layout->style().get("scroll-behavior");
                                                if (sb && sb->type == css::CSSValue::Type::KEYWORD &&
                                                    sb->keyword == "smooth")
                                                    smooth = true;
                                                if (smooth) {
                                                    chrome_.scroll_target_y = target;
                                                } else {
                                                    chrome_.scroll_y = target;
                                                }
                                            }
                                        }
                                    }
                                } else if (chrome_.ctrl_down) {
                                    // Ctrl+click opens the link in a new background tab
                                    open_background_tab(url);
                                } else {
                                    navigate(url);
                                }
                            }
                        } else {
                            html::g_form_state.blur();
                        }
                    }  // end of 'if (!disabled)'
                } else {
                    html::g_form_state.blur();
                }
            } else {
                html::g_form_state.blur();
            }
            return;
        }

        auto &r = chrome_.rects;

        if (is_in_rect(mx, my, r.close_btn)) {
            SendMessage((HWND)window_->get_native_handle(), WM_CLOSE, 0, 0);
            return;
        }
        if (is_in_rect(mx, my, r.maximize_btn)) {
            auto *w32 = static_cast<platform::Win32Window *>(window_.get());
            w32->toggle_maximize();
            return;
        }
        if (is_in_rect(mx, my, r.minimize_btn)) {
            ShowWindow((HWND)window_->get_native_handle(), SW_MINIMIZE);
            return;
        }

        for (u32 i = 0; i < r.tab_close.size(); i++) {
            if (is_in_rect(mx, my, r.tab_close[i])) {
                close_tab(i);
                return;
            }
        }

        i32 tab = tab_index_at(mx, my);
        if (tab >= 0) {
            // Double-click on a tab toggles window maximize (mainstream
            // browser behavior). Reuse the platform double-click time.
            auto now = std::chrono::steady_clock::now();
            u64 ms = static_cast<u64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            if (ms - chrome_.last_tab_click_ms <= GetDoubleClickTime() && chrome_.last_tab_click_idx == tab) {
                auto *w32 = static_cast<platform::Win32Window *>(window_.get());
                w32->toggle_maximize();
            } else {
                select_tab(static_cast<u32>(tab));
            }
            chrome_.last_tab_click_ms = ms;
            chrome_.last_tab_click_idx = tab;
            return;
        }
        chrome_.last_tab_click_idx = -1;

        if (new_tab_button_hit(mx, my)) {
            new_tab();
            return;
        }

        if (is_in_rect(mx, my, r.back)) {
            navigate_back();
            return;
        }
        if (is_in_rect(mx, my, r.forward)) {
            navigate_forward();
            return;
        }
        if (is_in_rect(mx, my, r.refresh)) {
            // The button is a stop control while a load is in flight.
            if (chrome_.is_loading && page_loader_)
                page_loader_->cancel();
            else
                refresh();
            return;
        }

        if (is_in_rect(mx, my, r.address)) {
            // Track the click cadence: single = select-all (or caret if
            // already focused), double = select word, triple+ = select all.
            auto now = std::chrono::steady_clock::now();
            u64 ms =
                static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            if (ms - chrome_.last_addr_click_ms <= GetDoubleClickTime() &&
                std::abs(mx - chrome_.last_addr_click_x) <= 4 && std::abs(my - chrome_.last_addr_click_y) <= 4)
                chrome_.addr_click_count++;
            else
                chrome_.addr_click_count = 1;
            chrome_.last_addr_click_ms = ms;
            chrome_.last_addr_click_x = mx;
            chrome_.last_addr_click_y = my;

            if (chrome_.addr_click_count >= 3) {
                focus_address_bar(true);
                return;
            }
            if (chrome_.addr_click_count == 2) {
                // Double-click: select the word under the pointer. Find the
                // byte offset whose rendered x is nearest the click, then
                // expand over ASCII word characters (punctuation like '.' '/'
                // ':' acts as a boundary, like mainstream browsers).
                const std::string &buf = chrome_.address_bar.edit_buffer;
                if (!buf.empty()) {
                    f32 tx = r.address.x + 6;
                    u32 pos = 0;
                    f32 best_d = 1e9f;
                    for (u32 i = 0; i <= buf.size(); i++) {
                        f32 cx = tx + text_renderer_->measure_text(buf.substr(0, i), 13);
                        f32 d = std::fabs(cx - static_cast<f32>(mx));
                        if (d < best_d) {
                            best_d = d;
                            pos = i;
                        }
                    }
                    u32 start = pos;
                    while (start > 0 && addr_word_char(static_cast<unsigned char>(buf[start - 1]))) start--;
                    u32 end = pos;
                    while (end < buf.size() && addr_word_char(static_cast<unsigned char>(buf[end]))) end++;
                    chrome_.address_bar.sel_start = start;
                    chrome_.address_bar.cursor_pos = end;
                    chrome_.address_bar.all_selected = false;
                }
                return;
            }
            // First click selects the whole URL so typing replaces it; a
            // second click drops the highlight to place the caret.
            if (chrome_.address_focused) {
                chrome_.address_bar.all_selected = false;
                chrome_.address_bar.sel_start = chrome_.address_bar.cursor_pos;
            } else {
                focus_address_bar(true);
            }
            return;
        }

        if (is_in_rect(mx, my, r.download)) {
            chrome_.show_downloads = !chrome_.show_downloads;
            chrome_.show_bookmarks_dropdown = false;
            chrome_.hovered_bookmark_item = -1;
            chrome_.bookmark_scroll_offset = 0.0f;
            return;
        }

        if (is_in_rect(mx, my, r.bookmark)) {
            handle_bookmark_click();
            return;
        }

        if (is_in_rect(mx, my, r.bookmark_chevron)) {
            chrome_.show_bookmarks_dropdown = !chrome_.show_bookmarks_dropdown;
            chrome_.hovered_bookmark_item = -1;
            chrome_.bookmark_scroll_offset = 0.0f;
            return;
        }

        if (is_in_rect(mx, my, r.menu)) {
            chrome_.show_menu = !chrome_.show_menu;
            chrome_.show_bookmarks_dropdown = false;
            chrome_.hovered_bookmark_item = -1;
            chrome_.bookmark_scroll_offset = 0.0f;
            return;
        }

        chrome_.show_bookmarks_dropdown = false;
        chrome_.hovered_bookmark_item = -1;
        chrome_.bookmark_scroll_offset = 0.0f;
        chrome_.address_focused = false;
    }

    void BrowserWindow::handle_key_down(const platform::Event &e) {
        if (e.key == platform::KeyCode::F && chrome_.ctrl_down && chrome_.shift_down) {
            renderer_->toggle_fps_overlay();
            return;
        }
        // Ctrl+Shift+S: save viewport screenshot
        if (e.key == platform::KeyCode::S && chrome_.ctrl_down && chrome_.shift_down) {
            int sw = static_cast<int>(viewport_width_);
            int sh = static_cast<int>(viewport_height_);
            std::vector<u8> pixels(static_cast<size_t>(sw * sh * 4));
            ::glReadPixels(0, 0, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            auto wu32 = [](std::vector<u8> &d, u32 v) {
                d.push_back(v & 0xFF);
                d.push_back((v >> 8) & 0xFF);
                d.push_back((v >> 16) & 0xFF);
                d.push_back((v >> 24) & 0xFF);
            };
            auto wu16 = [](std::vector<u8> &d, u16 v) {
                d.push_back(v & 0xFF);
                d.push_back((v >> 8) & 0xFF);
            };
            std::vector<u8> bmp;
            u32 row = ((sw * 24 + 31) / 32) * 4;
            u32 ds = row * sh;
            bmp.push_back('B');
            bmp.push_back('M');
            wu32(bmp, 14 + 40 + ds);
            wu32(bmp, 0);
            wu32(bmp, 14 + 40);
            wu32(bmp, 40);
            wu32(bmp, sw);
            wu32(bmp, sh);
            wu16(bmp, 1);
            wu16(bmp, 24);
            wu32(bmp, 0);
            wu32(bmp, ds);
            wu32(bmp, 2835);
            wu32(bmp, 2835);
            wu32(bmp, 0);
            wu32(bmp, 0);
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < sw; x++) {
                    size_t idx = (static_cast<size_t>(y) * sw + static_cast<size_t>(x)) * 4;
                    bmp.push_back(pixels[idx + 2]);
                    bmp.push_back(pixels[idx + 1]);
                    bmp.push_back(pixels[idx + 0]);
                }
                for (u32 p = static_cast<u32>(sw) * 3; p < row; p++) bmp.push_back(0);
            }
            FILE *sf = fopen("viewport_screenshot.bmp", "wb");
            if (sf) {
                fwrite(bmp.data(), 1, bmp.size(), sf);
                fclose(sf);
            }
            return;
        }
        // Ctrl+Shift+X: copy all visible page text to clipboard (Unicode)
        if (e.key == platform::KeyCode::X && chrome_.ctrl_down && chrome_.shift_down) {
            if (current_page_.has_value() && current_page_->layout) {
                std::string all_text;
                auto collect_text = [&](const css::LayoutNode *node, auto &self) -> void {
                    if (node->is_text() && !node->text().empty()) {
                        if (!all_text.empty())
                            all_text += '\n';
                        all_text += node->text();
                    }
                    for (const auto &child : node->children) {
                        self(child.get(), self);
                    }
                };
                collect_text(current_page_->layout.get(), collect_text);
                if (!all_text.empty()) {
                    int wide_len = MultiByteToWideChar(CP_UTF8, 0, all_text.data(), (int)all_text.size(), nullptr, 0);
                    if (wide_len > 0) {
                        HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, (wide_len + 1) * sizeof(wchar_t));
                        if (hglb) {
                            wchar_t *buf = (wchar_t *)GlobalLock(hglb);
                            if (buf) {
                                MultiByteToWideChar(CP_UTF8, 0, all_text.data(), (int)all_text.size(), buf, wide_len);
                                buf[wide_len] = 0;
                            }
                            GlobalUnlock(hglb);
                            if (OpenClipboard(nullptr)) {
                                EmptyClipboard();
                                SetClipboardData(CF_UNICODETEXT, hglb);
                                CloseClipboard();
                            }
                        }
                    }
                }
            }
            return;
        }
        if (e.key == platform::KeyCode::T && chrome_.ctrl_down) {
            new_tab();
            return;
        }
        // Ctrl+W: close the current tab (global — closes the window when the
        // last tab is closed, like mainstream browsers)
        if (e.key == platform::KeyCode::W && chrome_.ctrl_down) {
            close_tab(chrome_.active_tab);
            return;
        }
        // Ctrl+D: bookmark / unbookmark the current page
        if (e.key == platform::KeyCode::D && chrome_.ctrl_down) {
            handle_bookmark_click();
            return;
        }
        // Ctrl+Tab / Ctrl+Shift+Tab: cycle through tabs
        if (e.key == platform::KeyCode::TAB && chrome_.ctrl_down) {
            u32 n = static_cast<u32>(chrome_.tabs.size());
            if (n > 1) {
                u32 idx = chrome_.shift_down ? (chrome_.active_tab + n - 1) % n : (chrome_.active_tab + 1) % n;
                select_tab(idx);
            }
            return;
        }
        // Ctrl+1..8: switch to the Nth tab; Ctrl+9: switch to the last tab
        if (chrome_.ctrl_down && e.key >= platform::KeyCode::_1 && e.key <= platform::KeyCode::_9) {
            if (!chrome_.tabs.empty()) {
                u32 target = (e.key == platform::KeyCode::_9)
                                 ? static_cast<u32>(chrome_.tabs.size()) - 1
                                 : static_cast<u32>(static_cast<int>(e.key) - static_cast<int>(platform::KeyCode::_1));
                select_tab(target);
            }
            return;
        }
        if (e.key == platform::KeyCode::R && chrome_.ctrl_down) {
            refresh();
            return;
        }
        if (e.key == platform::KeyCode::F5 && !chrome_.ctrl_down) {
            refresh();
            return;
        }
        if (e.key == platform::KeyCode::F && chrome_.ctrl_down && !chrome_.shift_down) {
            chrome_.address_focused = false;
            chrome_.address_bar.clear();
            chrome_.find_state.show();
            return;
        }
        if (e.key == platform::KeyCode::G && chrome_.ctrl_down) {
            if (chrome_.find_state.visible && !chrome_.find_state.query.empty()) {
                chrome_.find_state.next();
            }
            return;
        }
        // Ctrl+A: select all — in the omnibox it selects the URL, in the
        // page it selects all page text
        if (e.key == platform::KeyCode::A && chrome_.ctrl_down) {
            if (chrome_.address_focused) {
                chrome_.address_bar.cursor_pos = static_cast<u32>(chrome_.address_bar.edit_buffer.size());
                chrome_.address_bar.sel_start = 0;
                chrome_.address_bar.all_selected = true;
                return;
            }
            selection_.clear();
            if (current_page_.has_value() && current_page_->layout) {
                const css::LayoutNode *first = nullptr;
                const css::LayoutNode *last = nullptr;
                std::function<void(const css::LayoutNode *)> find_text = [&](const css::LayoutNode *node) {
                    if (has_user_select_none(node))
                        return;
                    if (node->is_text() && !node->text().empty()) {
                        if (!first)
                            first = node;
                        last = node;
                    }
                    for (auto &ch : node->children) find_text(ch.get());
                };
                find_text(current_page_->layout.get());
                if (first && last) {
                    // Collect all text into selected_text immediately so Ctrl+C
                    // doesn't need to touch the layout tree (avoiding dangling pointers).
                    selection_.active = true;
                    selection_.start_node = first;
                    selection_.end_node = last;
                    selection_.start_offset = 0;
                    selection_.end_offset = static_cast<u32>(last->text().size());
                    selection_.all_text = true;
                    bool collecting = false;
                    std::function<void(const css::LayoutNode *)> collect = [&](const css::LayoutNode *node) {
                        if (has_user_select_none(node))
                            return;
                        if (node == selection_.start_node)
                            collecting = true;
                        if (collecting && node->is_text() && !node->text().empty()) {
                            if (!selection_.selected_text.empty())
                                selection_.selected_text += '\n';
                            selection_.selected_text += node->text();
                        }
                        for (auto &ch : node->children) collect(ch.get());
                        if (node == selection_.end_node)
                            collecting = false;
                    };
                    collect(current_page_->layout.get());
                }
            }
            renderer_->set_needs_redraw();
            return;
        }
        // Ctrl+C: copy — omnibox selection first, then page selection
        if (e.key == platform::KeyCode::C && chrome_.ctrl_down) {
            if (chrome_.address_focused) {
                std::string sel = chrome_.address_bar.selected_text();
                clipboard_copy(sel.empty() ? chrome_.address_bar.edit_buffer : sel);
                return;
            }
            if (selection_.active && !selection_.selected_text.empty()) {
                int wide_len = MultiByteToWideChar(
                    CP_UTF8, 0, selection_.selected_text.data(), (int)selection_.selected_text.size(), nullptr, 0);
                if (wide_len > 0) {
                    HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, (wide_len + 1) * sizeof(wchar_t));
                    if (hglb) {
                        wchar_t *buf = (wchar_t *)GlobalLock(hglb);
                        if (buf) {
                            MultiByteToWideChar(CP_UTF8,
                                                0,
                                                selection_.selected_text.data(),
                                                (int)selection_.selected_text.size(),
                                                buf,
                                                wide_len);
                            buf[wide_len] = 0;
                        }
                        GlobalUnlock(hglb);
                        if (OpenClipboard(nullptr)) {
                            EmptyClipboard();
                            SetClipboardData(CF_UNICODETEXT, hglb);
                            CloseClipboard();
                        }
                    }
                }
            }
            return;
        }
        if (e.key == platform::KeyCode::L && chrome_.ctrl_down) {
            // Ctrl+L focuses the omnibox with the whole URL selected
            focus_address_bar(true);
            return;
        }
        if (e.key == platform::KeyCode::F12 && !chrome_.ctrl_down) {
            chrome_.devtools.visible = !chrome_.devtools.visible;
            return;
        }
        if (e.key == platform::KeyCode::F11 && !chrome_.ctrl_down) {
            chrome_.fullscreen = !chrome_.fullscreen;
            if (window_) {
                auto *w32 = static_cast<platform::Win32Window *>(window_.get());
                w32->set_fullscreen(chrome_.fullscreen);
            }
            return;
        }
        if (e.key == platform::KeyCode::F6 && !chrome_.ctrl_down) {
            focus_address_bar(true);
            return;
        }
        if ((e.key == platform::KeyCode::LEFT && chrome_.alt_down) ||
            (e.key == platform::KeyCode::BACKSPACE && chrome_.alt_down)) {
            navigate_back();
            return;
        }
        if (e.key == platform::KeyCode::RIGHT && chrome_.alt_down) {
            navigate_forward();
            return;
        }

        if (e.key == platform::KeyCode::_0 && chrome_.ctrl_down) {
            // Ctrl+0: reset zoom
            if (settings_) {
                settings_->set_zoom_level(1.0f);
                settings_->save_to_file(data_dir() + "/settings.txt");
            }
            return;
        }
        if (e.key == platform::KeyCode::EQUALS && chrome_.ctrl_down) {
            // Ctrl+= (or Ctrl++) zoom in
            if (settings_) {
                f32 z = settings_->zoom_level() * 1.25f;
                if (z > 5.0f)
                    z = 5.0f;
                settings_->set_zoom_level(z);
                settings_->save_to_file(data_dir() + "/settings.txt");
            }
            return;
        }
        if (e.key == platform::KeyCode::MINUS && chrome_.ctrl_down) {
            // Ctrl+- zoom out
            if (settings_) {
                f32 z = settings_->zoom_level() / 1.25f;
                if (z < 0.25f)
                    z = 0.25f;
                settings_->set_zoom_level(z);
                settings_->save_to_file(data_dir() + "/settings.txt");
            }
            return;
        }

        if (chrome_.find_state.visible) {
            if (e.key == platform::KeyCode::ESCAPE) {
                chrome_.find_state.hide();
            } else if (e.key == platform::KeyCode::ENTER && chrome_.shift_down) {
                if (!chrome_.find_state.query.empty()) {
                    chrome_.find_state.previous();
                }
            } else if (e.key == platform::KeyCode::ENTER) {
                if (!chrome_.find_state.query.empty()) {
                    chrome_.find_state.next();
                }
            } else if (e.key == platform::KeyCode::BACKSPACE) {
                if (!chrome_.find_state.query.empty()) {
                    chrome_.find_state.query.pop_back();
                    if (current_page_.has_value() && current_page_->dom) {
                        chrome_.find_state.search(current_page_->dom.get(), chrome_.find_state.query);
                    }
                }
            } else {
                char c = keycode_to_char(e.key, chrome_.shift_down);
                if (c) {
                    chrome_.find_state.query += c;
                    if (current_page_.has_value() && current_page_->dom) {
                        chrome_.find_state.search(current_page_->dom.get(), chrome_.find_state.query);
                    }
                }
            }
            return;
        }

        if (chrome_.address_focused) {
            // Erase the active selection (whole-buffer or word), collapsing
            // the caret to its start. Returns true when something was erased.
            auto erase_selection = [&]() -> bool {
                if (!chrome_.address_bar.has_selection())
                    return false;
                u32 a = std::min(chrome_.address_bar.sel_start, chrome_.address_bar.cursor_pos);
                u32 b = std::max(chrome_.address_bar.sel_start, chrome_.address_bar.cursor_pos);
                if (a > chrome_.address_bar.edit_buffer.size())
                    a = static_cast<u32>(chrome_.address_bar.edit_buffer.size());
                if (b > chrome_.address_bar.edit_buffer.size())
                    b = static_cast<u32>(chrome_.address_bar.edit_buffer.size());
                chrome_.address_bar.edit_buffer.erase(a, b - a);
                chrome_.address_bar.cursor_pos = a;
                chrome_.address_bar.sel_start = a;
                chrome_.address_bar.all_selected = false;
                return true;
            };
            if (e.key == platform::KeyCode::C && chrome_.ctrl_down) {
                std::string sel = chrome_.address_bar.selected_text();
                clipboard_copy(sel.empty() ? chrome_.address_bar.edit_buffer : sel);
                return;
            }
            if (e.key == platform::KeyCode::X && chrome_.ctrl_down) {
                std::string sel = chrome_.address_bar.selected_text();
                clipboard_copy(sel.empty() ? chrome_.address_bar.edit_buffer : sel);
                erase_selection();
                return;
            }
            if (e.key == platform::KeyCode::V && chrome_.ctrl_down) {
                std::string paste = clipboard_paste();
                if (!paste.empty()) {
                    erase_selection();
                    chrome_.address_bar.edit_buffer.insert(chrome_.address_bar.cursor_pos, paste);
                    chrome_.address_bar.cursor_pos += static_cast<u32>(paste.size());
                    chrome_.address_bar.sel_start = chrome_.address_bar.cursor_pos;
                }
                return;
            }
            if (e.key == platform::KeyCode::A && chrome_.ctrl_down) {
                chrome_.address_bar.cursor_pos = static_cast<u32>(chrome_.address_bar.edit_buffer.size());
                chrome_.address_bar.sel_start = 0;
                chrome_.address_bar.all_selected = true;
                return;
            }

            if (e.key == platform::KeyCode::ENTER) {
                navigate(chrome_.address_bar.edit_buffer);
                chrome_.address_focused = false;
                chrome_.address_bar.edit_buffer.clear();
                chrome_.address_bar.all_selected = false;
            } else if (e.key == platform::KeyCode::ESCAPE) {
                chrome_.address_focused = false;
                chrome_.address_bar.edit_buffer = chrome_.url;  // restore the live URL
                chrome_.address_bar.all_selected = false;
            } else if (e.key == platform::KeyCode::BACKSPACE && chrome_.ctrl_down) {
                // Ctrl+Backspace deletes the word before the caret
                if (!erase_selection()) {
                    std::string &buf = chrome_.address_bar.edit_buffer;
                    u32 pos = chrome_.address_bar.cursor_pos;
                    while (pos > 0 && !addr_word_char(static_cast<unsigned char>(buf[pos - 1]))) pos--;
                    while (pos > 0 && addr_word_char(static_cast<unsigned char>(buf[pos - 1]))) pos--;
                    buf.erase(pos, chrome_.address_bar.cursor_pos - pos);
                    chrome_.address_bar.cursor_pos = pos;
                    chrome_.address_bar.sel_start = pos;
                }
            } else if ((e.key == platform::KeyCode::BACKSPACE)) {
                if (!erase_selection() && chrome_.address_bar.cursor_pos > 0) {
                    chrome_.address_bar.edit_buffer.erase(chrome_.address_bar.cursor_pos - 1, 1);
                    chrome_.address_bar.cursor_pos--;
                    chrome_.address_bar.sel_start = chrome_.address_bar.cursor_pos;
                }
            } else if (e.key == platform::KeyCode::DELETE && !chrome_.ctrl_down) {
                if (!erase_selection() && chrome_.address_bar.cursor_pos < chrome_.address_bar.edit_buffer.length())
                    chrome_.address_bar.edit_buffer.erase(chrome_.address_bar.cursor_pos, 1);
            } else if (e.key == platform::KeyCode::LEFT && chrome_.ctrl_down) {
                // Ctrl+Left jumps to the start of the previous word
                const std::string &buf = chrome_.address_bar.edit_buffer;
                u32 pos = chrome_.address_bar.has_selection()
                              ? std::min(chrome_.address_bar.sel_start, chrome_.address_bar.cursor_pos)
                              : chrome_.address_bar.cursor_pos;
                while (pos > 0 && !addr_word_char(static_cast<unsigned char>(buf[pos - 1]))) pos--;
                while (pos > 0 && addr_word_char(static_cast<unsigned char>(buf[pos - 1]))) pos--;
                chrome_.address_bar.cursor_pos = pos;
                chrome_.address_bar.sel_start = pos;
                chrome_.address_bar.all_selected = false;
            } else if (e.key == platform::KeyCode::RIGHT && chrome_.ctrl_down) {
                // Ctrl+Right jumps to the start of the next word
                const std::string &buf = chrome_.address_bar.edit_buffer;
                u32 pos = chrome_.address_bar.cursor_pos;
                while (pos < buf.size() && !addr_word_char(static_cast<unsigned char>(buf[pos]))) pos++;
                while (pos < buf.size() && addr_word_char(static_cast<unsigned char>(buf[pos]))) pos++;
                chrome_.address_bar.cursor_pos = pos;
                chrome_.address_bar.sel_start = pos;
                chrome_.address_bar.all_selected = false;
            } else if (e.key == platform::KeyCode::LEFT && !chrome_.ctrl_down) {
                // Collapse to the selection edge, then move
                u32 a = std::min(chrome_.address_bar.sel_start, chrome_.address_bar.cursor_pos);
                u32 b = std::max(chrome_.address_bar.sel_start, chrome_.address_bar.cursor_pos);
                chrome_.address_bar.cursor_pos = chrome_.address_bar.has_selection() ? a : b;
                if (chrome_.address_bar.cursor_pos > 0)
                    chrome_.address_bar.cursor_pos--;
                chrome_.address_bar.sel_start = chrome_.address_bar.cursor_pos;
                chrome_.address_bar.all_selected = false;
            } else if (e.key == platform::KeyCode::RIGHT && !chrome_.ctrl_down) {
                u32 b = std::max(chrome_.address_bar.sel_start, chrome_.address_bar.cursor_pos);
                chrome_.address_bar.cursor_pos = b;
                if (chrome_.address_bar.cursor_pos < chrome_.address_bar.edit_buffer.length())
                    chrome_.address_bar.cursor_pos++;
                chrome_.address_bar.sel_start = chrome_.address_bar.cursor_pos;
                chrome_.address_bar.all_selected = false;
            } else if (e.key == platform::KeyCode::HOME) {
                chrome_.address_bar.cursor_pos = 0;
                chrome_.address_bar.sel_start = 0;
                chrome_.address_bar.all_selected = false;
            } else if (e.key == platform::KeyCode::END) {
                chrome_.address_bar.cursor_pos = static_cast<u32>(chrome_.address_bar.edit_buffer.length());
                chrome_.address_bar.sel_start = chrome_.address_bar.cursor_pos;
                chrome_.address_bar.all_selected = false;
            } else {
                char c = keycode_to_char(e.key, chrome_.shift_down);
                if (c) {
                    erase_selection();
                    chrome_.address_bar.edit_buffer.insert(chrome_.address_bar.cursor_pos, 1, c);
                    chrome_.address_bar.cursor_pos++;
                    chrome_.address_bar.sel_start = chrome_.address_bar.cursor_pos;
                }
            }
        } else if (html::g_form_state.focused_element) {
            // Silently blur if element became disabled
            if (html::g_form_state.focused_element->has_attribute("disabled")) {
                html::g_form_state.blur();
                return;
            }
            // Route keyboard to focused form element
            auto *el = html::g_form_state.focused_element;
            std::string tag = el->tag_name;
            std::string type = el->get_attribute("type");

            if (e.key == platform::KeyCode::TAB) {
                html::g_form_state.blur();
                return;
            }
            if (e.key == platform::KeyCode::ESCAPE) {
                html::g_form_state.blur();
                return;
            }

            if (tag == "input" && (type.empty() || type == "text" || type == "password" || type == "email" ||
                                   type == "search" || type == "url" || type == "number")) {
                auto maxlen_attr = el->get_attribute("maxlength");
                int maxlen = maxlen_attr.empty() ? -1 : std::atoi(maxlen_attr.c_str());
                if (e.key == platform::KeyCode::ENTER) {
                    {
                        std::string nav_url = html::handle_form_submission(el);
                        if (!nav_url.empty())
                            start_load(nav_url);
                    }
                    html::g_form_state.blur();
                } else if (e.key == platform::KeyCode::BACKSPACE) {
                    std::string val = html::g_form_state.get_value(el);
                    if (!val.empty() && html::g_form_state.caret_position > 0) {
                        val.erase(val.begin() + static_cast<i64>(html::g_form_state.caret_position) - 1);
                        html::g_form_state.caret_position--;
                        html::g_form_state.set_value(el, val);
                    }
                } else if (e.key == platform::KeyCode::LEFT) {
                    if (html::g_form_state.caret_position > 0)
                        html::g_form_state.caret_position--;
                } else if (e.key == platform::KeyCode::RIGHT) {
                    std::string val = html::g_form_state.get_value(el);
                    if (html::g_form_state.caret_position < val.size())
                        html::g_form_state.caret_position++;
                } else if (e.key == platform::KeyCode::UP && type == "number") {
                    adjust_number_value(el, 1);
                } else if (e.key == platform::KeyCode::DOWN && type == "number") {
                    adjust_number_value(el, -1);
                } else if (e.key == platform::KeyCode::HOME) {
                    html::g_form_state.caret_position = 0;
                } else if (e.key == platform::KeyCode::END) {
                    std::string val = html::g_form_state.get_value(el);
                    html::g_form_state.caret_position = static_cast<u32>(val.size());
                } else if (e.key == platform::KeyCode::V && chrome_.ctrl_down) {
                    std::string paste = clipboard_paste();
                    if (!paste.empty()) {
                        std::string val = html::g_form_state.get_value(el);
                        if (maxlen >= 0) {
                            int available = maxlen - static_cast<int>(val.size());
                            if (available <= 0)
                                paste.clear();
                            else if (static_cast<int>(paste.size()) > available)
                                paste = paste.substr(0, static_cast<size_t>(available));
                        }
                        if (!paste.empty()) {
                            val.insert(html::g_form_state.caret_position, paste);
                            html::g_form_state.caret_position += static_cast<u32>(paste.size());
                            html::g_form_state.set_value(el, val);
                        }
                    }
                } else if (e.key == platform::KeyCode::C && chrome_.ctrl_down) {
                    std::string val = html::g_form_state.get_value(el);
                    clipboard_copy(val);
                } else {
                    char c = keycode_to_char(e.key, chrome_.shift_down);
                    if (c) {
                        // For number inputs, only allow valid number characters
                        if (type == "number") {
                            if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' ||
                                  c == 'e' || c == 'E'))
                                c = '\0';
                        }
                        if (c) {
                            std::string val = html::g_form_state.get_value(el);
                            if (maxlen < 0 || static_cast<int>(val.size()) < maxlen) {
                                val.insert(html::g_form_state.caret_position, 1, c);
                                html::g_form_state.caret_position++;
                                html::g_form_state.set_value(el, val);
                            }
                        }
                    }
                }
            } else if (tag == "input" && type == "range") {
                f32 min_val = 0, max_val = 100;
                std::string min_str = el->get_attribute("min");
                std::string max_str = el->get_attribute("max");
                std::string step_str = el->get_attribute("step");
                if (!min_str.empty())
                    min_val = std::strtof(min_str.c_str(), nullptr);
                if (!max_str.empty())
                    max_val = std::strtof(max_str.c_str(), nullptr);
                f32 step = step_str.empty() ? 1.0f : std::strtof(step_str.c_str(), nullptr);
                if (step <= 0)
                    step = 1.0f;
                std::string val = html::g_form_state.get_value(el);
                f32 cur = std::strtof(val.c_str(), nullptr);
                if (e.key == platform::KeyCode::LEFT) {
                    cur -= step;
                } else if (e.key == platform::KeyCode::RIGHT) {
                    cur += step;
                } else if (e.key == platform::KeyCode::UP) {
                    cur += step;
                } else if (e.key == platform::KeyCode::DOWN) {
                    cur -= step;
                }
                if (cur < min_val)
                    cur = min_val;
                if (cur > max_val)
                    cur = max_val;
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", cur);
                html::g_form_state.set_value(el, buf);
            } else if (tag == "textarea") {
                if (e.key == platform::KeyCode::ENTER) {
                    std::string val = html::g_form_state.get_value(el);
                    val.insert(html::g_form_state.caret_position, 1, '\n');
                    html::g_form_state.caret_position++;
                    html::g_form_state.set_value(el, val);
                } else if (e.key == platform::KeyCode::BACKSPACE) {
                    std::string val = html::g_form_state.get_value(el);
                    if (!val.empty() && html::g_form_state.caret_position > 0) {
                        val.erase(val.begin() + static_cast<i64>(html::g_form_state.caret_position) - 1);
                        html::g_form_state.caret_position--;
                        html::g_form_state.set_value(el, val);
                    }
                } else if (e.key == platform::KeyCode::LEFT) {
                    if (html::g_form_state.caret_position > 0)
                        html::g_form_state.caret_position--;
                } else if (e.key == platform::KeyCode::RIGHT) {
                    std::string val = html::g_form_state.get_value(el);
                    if (html::g_form_state.caret_position < val.size())
                        html::g_form_state.caret_position++;
                } else if (e.key == platform::KeyCode::HOME) {
                    html::g_form_state.caret_position = 0;
                } else if (e.key == platform::KeyCode::END) {
                    std::string val = html::g_form_state.get_value(el);
                    html::g_form_state.caret_position = static_cast<u32>(val.size());
                } else {
                    char c = keycode_to_char(e.key, chrome_.shift_down);
                    if (c) {
                        std::string val = html::g_form_state.get_value(el);
                        val.insert(html::g_form_state.caret_position, 1, c);
                        html::g_form_state.caret_position++;
                        html::g_form_state.set_value(el, val);
                    }
                }
            } else if (tag == "input" && type == "checkbox") {
                if (e.key == platform::KeyCode::SPACE) {
                    html::g_form_state.toggle_checkbox(el);
                }
            } else if (tag == "input" && type == "submit") {
                if (e.key == platform::KeyCode::ENTER || e.key == platform::KeyCode::SPACE) {
                    {
                        std::string nav_url = html::handle_form_submission(el);
                        if (!nav_url.empty())
                            start_load(nav_url);
                    }
                    html::g_form_state.blur();
                }
            } else if (tag == "button") {
                if (e.key == platform::KeyCode::ENTER || e.key == platform::KeyCode::SPACE) {
                    {
                        std::string nav_url = html::handle_form_submission(el);
                        if (!nav_url.empty())
                            start_load(nav_url);
                    }
                    html::g_form_state.blur();
                }
            } else if (tag == "select") {
                if (e.key == platform::KeyCode::DOWN || e.key == platform::KeyCode::RIGHT) {
                    int idx = html::g_form_state.get_selected_index(el);
                    html::g_form_state.set_selected_index(el, idx + 1);
                } else if (e.key == platform::KeyCode::UP || e.key == platform::KeyCode::LEFT) {
                    int idx = html::g_form_state.get_selected_index(el);
                    if (idx > 0)
                        html::g_form_state.set_selected_index(el, idx - 1);
                }
            }
        } else {
            if (chrome_.devtools.visible && chrome_.devtools.active_tab == DevToolsState::CONSOLE) {
                if (e.key == platform::KeyCode::ENTER) {
                    std::string code = chrome_.devtools.console_input;
                    chrome_.devtools.console_input.clear();
                    chrome_.devtools.add_console_entry(DevToolsState::ConsoleEntry::LOG, "> " + code);
                    chrome_.devtools.add_console_entry(DevToolsState::ConsoleEntry::LOG,
                                                       "  <- (execution not available in this context)");
                    return;
                }
                if (e.key == platform::KeyCode::ESCAPE) {
                    chrome_.devtools.visible = false;
                    return;
                }
                if (e.key == platform::KeyCode::BACKSPACE) {
                    if (!chrome_.devtools.console_input.empty()) {
                        chrome_.devtools.console_input.pop_back();
                    }
                    return;
                }
                char c = keycode_to_char(e.key, chrome_.shift_down);
                if (c) {
                    chrome_.devtools.console_input += c;
                    return;
                }
            }

            if (e.key == platform::KeyCode::ESCAPE) {
                if (chrome_.show_context_menu) {
                    chrome_.show_context_menu = false;
                    chrome_.hovered_context_item = -1;
                    return;
                }
                if (chrome_.is_loading && page_loader_) {
                    // Esc stops the in-flight load first, like mainstream browsers
                    page_loader_->cancel();
                    return;
                }
                if (chrome_.show_downloads) {
                    chrome_.show_downloads = false;
                    return;
                }
                if (chrome_.find_state.visible) {
                    chrome_.find_state.hide();
                    return;
                }
                if (chrome_.devtools.visible) {
                    chrome_.devtools.visible = false;
                    return;
                }
                chrome_.show_settings = false;
                chrome_.show_menu = false;
            }
            f32 content_h = static_cast<f32>(viewport_height_) - chrome_height();
            i32 page_step = static_cast<i32>(content_h * 0.9f);
            if (e.key == platform::KeyCode::PAGE_DOWN ||
                (e.key == platform::KeyCode::SPACE && !chrome_.address_focused)) {
                handle_scroll(-1 * page_step / 30);
            } else if (e.key == platform::KeyCode::PAGE_UP) {
                handle_scroll(1 * page_step / 30);
            } else if (e.key == platform::KeyCode::HOME) {
                chrome_.scroll_y = 0;
            } else if (e.key == platform::KeyCode::END) {
                chrome_.scroll_y = chrome_.scroll_max;
            } else if (e.key == platform::KeyCode::UP) {
                handle_scroll(3);
            } else if (e.key == platform::KeyCode::DOWN) {
                handle_scroll(-3);
            }
        }
    }

    void BrowserWindow::handle_mouse_move(i32 mx, i32 my) {
        // Handle textarea resize drag
        if (chrome_.textarea_resize.active && chrome_.textarea_resize.element) {
            f32 px = static_cast<f32>(mx);
            f32 py = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
            f32 dx = px - chrome_.textarea_resize.start_mouse_x;
            f32 dy = py - chrome_.textarea_resize.start_mouse_y;

            std::string resize = "both";
            if (chrome_.textarea_resize.layout_node) {
                auto *rs = chrome_.textarea_resize.layout_node->style().get("resize");
                if (rs && rs->type == css::CSSValue::Type::KEYWORD) {
                    resize = rs->keyword;
                }
            }

            f32 new_w = chrome_.textarea_resize.start_width;
            f32 new_h = chrome_.textarea_resize.start_height;
            if (resize == "both" || resize == "horizontal") {
                new_w = std::max(20.0f, chrome_.textarea_resize.start_width + dx);
            }
            if (resize == "both" || resize == "vertical") {
                new_h = std::max(20.0f, chrome_.textarea_resize.start_height + dy);
            }

            // Update the element's inline style with new width/height
            auto *el = chrome_.textarea_resize.element;
            std::string style = el->get_attribute("style");
            // Remove existing width/height from inline style
            std::string new_style;
            size_t pos = 0;
            while (pos < style.size()) {
                size_t end = style.find(';', pos);
                if (end == std::string::npos)
                    end = style.size();
                std::string decl = style.substr(pos, end - pos);
                while (!decl.empty() && decl[0] == ' ') decl = decl.substr(1);
                if (!decl.empty()) {
                    std::string lower;
                    for (char c : decl) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (lower.find("width:") != 0 && lower.find("height:") != 0) {
                        if (!new_style.empty())
                            new_style += "; ";
                        new_style += decl;
                    }
                }
                pos = end + 1;
            }
            if (!new_style.empty())
                new_style += "; ";
            new_style += "width:" + std::to_string(new_w) + "px; height:" + std::to_string(new_h) + "px";
            el->attributes["style"] = new_style;

            // BR-P3: defer the relayout to the frame loop — one relayout per
            // rendered frame instead of one per mouse-move event.
            relayout_pending_ = true;
            frame_dirty_ = true;
            return;
        }

        chrome_.hovered_button = -1;
        chrome_.hovered_tab = -1;
        chrome_.hovered_close = -1;
        chrome_.hovered_menu_item = -1;
        if (my > chrome_height()) {
            update_tab_tooltip(-1, -1);
            chrome_.hovered_menu_item = -1;
            // Track hover in bookmarks dropdown
            chrome_.hovered_bookmark_delete = -1;
            if (chrome_.show_bookmarks_dropdown) {
                constexpr f32 ITEM_H = 28.0f, HEADER_H = 28.0f, PAD = 4.0f;
                constexpr f32 DEL_W = 22.0f;
                auto dd = bookmarks_dropdown_rect();
                auto all = bookmarks_ ? bookmarks_->all() : std::vector<Bookmark>();

                if (is_in_rect(mx, my, dd)) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    f32 rel_y = static_cast<f32>(my) - dd.y - PAD - HEADER_H;
                    if (rel_y >= 0.0f) {
                        f32 scrolled_y = rel_y + chrome_.bookmark_scroll_offset;
                        i32 idx = static_cast<i32>(scrolled_y / ITEM_H);
                        if (idx >= 0 && idx < static_cast<i32>(all.size())) {
                            chrome_.hovered_bookmark_item = idx;
                            // Delete "×" zone on the right edge of the row
                            if (static_cast<f32>(mx) >= dd.x + dd.w - DEL_W)
                                chrome_.hovered_bookmark_delete = idx;
                        } else {
                            chrome_.hovered_bookmark_item = -1;
                        }
                    } else {
                        chrome_.hovered_bookmark_item = -1;
                    }
                    return;
                }
            }
            chrome_.hovered_bookmark_item = -1;

            // Track context menu hover
            if (chrome_.show_context_menu) {
                auto r = context_menu_rect();
                if (is_in_rect(mx, my, r)) {
                    constexpr f32 PAD = 4.0f;
                    i32 idx = static_cast<i32>((static_cast<f32>(my) - r.y - PAD) / ChromeUI::CTX_ITEM_H);
                    u32 items = chrome_.context_on_link ? 2 : 3;
                    chrome_.hovered_context_item = (idx >= 0 && idx < static_cast<i32>(items)) ? idx : -1;
                } else {
                    chrome_.hovered_context_item = -1;
                }
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return;
            }

            // Track menu item hover
            if (chrome_.show_menu) {
                auto geom = ChromeUI::menu_geometry(static_cast<f32>(viewport_width_),
                                                    static_cast<f32>(viewport_height_),
                                                    chrome_height(),
                                                    chrome_.rects.menu);
                if (mx >= geom.x && mx <= geom.x + geom.w && my >= geom.y && my <= geom.y + geom.h) {
                    constexpr f32 PAD = 4.0f;
                    f32 rel_y = static_cast<f32>(my) - geom.y - PAD;
                    i32 idx = static_cast<i32>(rel_y / ChromeUI::MENU_ITEM_H);
                    chrome_.hovered_menu_item =
                        (idx >= 0 && idx < static_cast<i32>(ChromeUI::MENU_ITEM_COUNT)) ? idx : -1;
                } else {
                    chrome_.hovered_menu_item = -1;
                }
            }

            // Set cursor based on page content under mouse
            if (current_page_.has_value() && current_page_->layout) {
                f32 py = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                auto ht = html::hit_test(current_page_->layout.get(), static_cast<f32>(mx), py);
                if (ht.element) {
                    std::string tag = ht.element->tag_name;

                    // Check CSS cursor property first (inherited)
                    HCURSOR css_cursor = nullptr;
                    if (ht.layout_node) {
                        auto *cursor_val = ht.layout_node->style().get("cursor");
                        if (cursor_val && cursor_val->type == css::CSSValue::Type::KEYWORD) {
                            const std::string &c = cursor_val->keyword;
                            if (c == "pointer")
                                css_cursor = LoadCursor(nullptr, IDC_HAND);
                            else if (c == "text" || c == "vertical-text")
                                css_cursor = LoadCursor(nullptr, IDC_IBEAM);
                            else if (c == "default" || c == "auto")
                                css_cursor = LoadCursor(nullptr, IDC_ARROW);
                            else if (c == "crosshair")
                                css_cursor = LoadCursor(nullptr, IDC_CROSS);
                            else if (c == "move")
                                css_cursor = LoadCursor(nullptr, IDC_SIZEALL);
                            else if (c == "wait" || c == "progress")
                                css_cursor = LoadCursor(nullptr, IDC_WAIT);
                            else if (c == "help")
                                css_cursor = LoadCursor(nullptr, IDC_HELP);
                            else if (c == "not-allowed" || c == "no-drop")
                                css_cursor = LoadCursor(nullptr, IDC_NO);
                            else if (c == "col-resize")
                                css_cursor = LoadCursor(nullptr, IDC_SIZEWE);
                            else if (c == "row-resize" || c == "ns-resize")
                                css_cursor = LoadCursor(nullptr, IDC_SIZENS);
                            else if (c == "e-resize" || c == "w-resize" || c == "ew-resize")
                                css_cursor = LoadCursor(nullptr, IDC_SIZEWE);
                            else if (c == "n-resize" || c == "s-resize")
                                css_cursor = LoadCursor(nullptr, IDC_SIZENS);
                            else if (c == "ne-resize" || c == "sw-resize")
                                css_cursor = LoadCursor(nullptr, IDC_SIZENESW);
                            else if (c == "nw-resize" || c == "se-resize")
                                css_cursor = LoadCursor(nullptr, IDC_SIZENWSE);
                            else if (c == "grab" || c == "grabbing")
                                css_cursor = LoadCursor(nullptr, IDC_HAND);
                            else if (c == "zoom-in" || c == "zoom-out")
                                css_cursor = LoadCursor(nullptr, IDC_SIZEALL);
                            else if (c == "none")
                                css_cursor = LoadCursor(nullptr, IDC_ARROW);  // hide not supported
                        }
                    }

                    if (css_cursor) {
                        SetCursor(css_cursor);
                    } else if (tag == "a")
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                    else if (tag == "input" || tag == "textarea")
                        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                    else {
                        // Check if element contains text
                        bool has_text = false;
                        for (auto &ch : ht.element->children)
                            if (ch->type == html::NodeType::TEXT) {
                                has_text = true;
                                break;
                            }
                        SetCursor(LoadCursor(nullptr, has_text ? IDC_IBEAM : IDC_ARROW));
                    }
                } else {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                }
            }
            return;
        }
        update_tab_tooltip(mx, my);

        for (u32 i = 0; i < chrome_.rects.tab_close.size(); i++) {
            if (is_in_rect(mx, my, chrome_.rects.tab_close[i])) {
                chrome_.hovered_close = static_cast<i32>(i);
                return;
            }
        }

        i32 tab = tab_index_at(mx, my);
        if (tab >= 0) {
            chrome_.hovered_tab = tab;
            return;
        }

        auto &r = chrome_.rects;
        if (is_in_rect(mx, my, r.close_btn))
            chrome_.hovered_button = ChromeUI::CLOSE;
        else if (is_in_rect(mx, my, r.maximize_btn))
            chrome_.hovered_button = ChromeUI::MAXIMIZE;
        else if (is_in_rect(mx, my, r.minimize_btn))
            chrome_.hovered_button = ChromeUI::MINIMIZE;
        else if (is_in_rect(mx, my, r.back))
            chrome_.hovered_button = ChromeUI::BACK;
        else if (is_in_rect(mx, my, r.forward))
            chrome_.hovered_button = ChromeUI::FORWARD;
        else if (is_in_rect(mx, my, r.refresh))
            chrome_.hovered_button = ChromeUI::REFRESH;
        else if (is_in_rect(mx, my, r.download))
            chrome_.hovered_button = ChromeUI::DOWNLOAD;
        else if (is_in_rect(mx, my, r.bookmark))
            chrome_.hovered_button = ChromeUI::BOOKMARK;
        else if (is_in_rect(mx, my, r.bookmark_chevron))
            chrome_.hovered_button = ChromeUI::BOOKMARK_CHEVRON;
        else if (is_in_rect(mx, my, r.menu))
            chrome_.hovered_button = ChromeUI::MENU;

        if (new_tab_button_hit(mx, my))
            chrome_.hovered_button = ChromeUI::REFRESH + 1;

        // Text cursor over the omnibox, arrow everywhere else in the chrome
        SetCursor(LoadCursor(nullptr, is_in_rect(mx, my, r.address) ? IDC_IBEAM : IDC_ARROW));
    }

    void BrowserWindow::handle_scroll(i32 delta) {
        // If bookmarks dropdown is open and mouse is over it, scroll the dropdown
        if (chrome_.show_bookmarks_dropdown) {
            POINT pt;
            GetCursorPos(&pt);
            HWND hwnd = static_cast<HWND>(window_->get_native_handle());
            ScreenToClient(hwnd, &pt);
            i32 mx = pt.x;
            i32 my = pt.y;

            constexpr f32 ITEM_H = 28.0f;
            auto dd = bookmarks_dropdown_rect();
            auto all = bookmarks_ ? bookmarks_->all() : std::vector<Bookmark>();
            f32 header_h = 28.0f;

            if (is_in_rect(mx, my, dd)) {
                f32 total_list_h = static_cast<f32>(all.size()) * ITEM_H;
                f32 visible_list_h = dd.h - header_h - 8.0f;
                f32 max_scroll = std::max(0.0f, total_list_h - visible_list_h);
                chrome_.bookmark_scroll_offset =
                    std::max(0.0f, std::min(max_scroll, chrome_.bookmark_scroll_offset - delta * 20.0f));
                return;
            }
        }

        // Wheeling anywhere else while a popup is open dismisses it
        if (popup_open()) {
            close_popups();
            return;
        }

        chrome_.scroll_y = std::max(0, std::min(chrome_.scroll_max, static_cast<i32>(chrome_.scroll_y - delta * 30)));
    }

    void BrowserWindow::handle_bookmark_click() {
        if (!bookmarks_)
            return;
        if (bookmarks_->is_bookmarked(chrome_.url)) {
            bookmarks_->remove(chrome_.url);
        } else {
            std::string title = chrome_.url;
            if (current_page_.has_value()) {
                auto &p = current_page_.value();
                if (!p.page_title.empty())
                    title = p.page_title;
            }
            bookmarks_->add(chrome_.url, title);
        }
        bookmarks_->save_to_file(BookmarkManager::default_path());
        chrome_.is_bookmarked = bookmarks_->is_bookmarked(chrome_.url);
    }

}  // namespace browser
