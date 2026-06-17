#include "../../html/form_state.hpp"
#include "../../html/form_submission.hpp"
#include "../../html/hit_test.hpp"
#include "../../platform/window_win32.hpp"
#include "../bookmarks.hpp"
#include "../settings.hpp"
#include "window.hpp"

#include <windows.h>

namespace browser {

    namespace {

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
            HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            if (!hglb)
                return;
            char *buf = (char *)GlobalLock(hglb);
            if (buf) {
                memcpy(buf, text.data(), text.size());
                buf[text.size()] = 0;
            }
            GlobalUnlock(hglb);
            if (OpenClipboard(nullptr)) {
                EmptyClipboard();
                SetClipboardData(CF_TEXT, hglb);
                CloseClipboard();
            }
        }

        std::string clipboard_paste() {
            std::string result;
            if (OpenClipboard(nullptr)) {
                HANDLE h = GetClipboardData(CF_TEXT);
                if (h) {
                    char *text = (char *)GlobalLock(h);
                    if (text) {
                        result = text;
                    }
                    GlobalUnlock(h);
                }
                CloseClipboard();
            }
            return result;
        }

    }  // namespace

    void BrowserWindow::handle_event(const platform::Event &e) {
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
            handle_mouse_click(e.mouse_x, e.mouse_y);
        } else if (e.type == platform::Event::Type::MOUSE_UP) {
            chrome_.scroll_dragging = false;
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
            if (compositor_)
                compositor_->set_viewport(static_cast<i32>(e.width), static_cast<i32>(e.height));
            compute_layout();
            if (page_loader_)
                page_loader_->set_viewport_size(viewport_width_, viewport_height_);
        }
    }

    void BrowserWindow::handle_mouse_click(i32 mx, i32 my) {
        {
            static FILE *f = nullptr;
            if (!f) {
                f = fopen("click_debug.txt", "w");
            }
            if (f) {
                fprintf(f,
                        "click: mx=%d my=%d chrome_h=%.0f show_settings=%d show_menu=%d address_focused=%d\n",
                        mx,
                        my,
                        chrome_height(),
                        (int)chrome_.show_settings,
                        (int)chrome_.show_menu,
                        (int)chrome_.address_focused);
                fflush(f);
            }
        }
        if (my > chrome_height()) {
            if (chrome_.show_settings) {
                f32 ox = 40, oy = chrome_height() + 20;
                if (is_in_rect(mx, my, {ox + 155, oy + 46, 80, 22})) {
                    Theme::toggle();
                    set_theme(Theme::current);
                    if (settings_) {
                        settings_->set_theme(Theme::current);
                        settings_->save_to_file("./settings.txt");
                    }
                    chrome_.address_focused = false;
                    return;
                }
            }
            if (chrome_.scroll_max > 0 && is_in_rect(mx, my, chrome_.rects.scrollbar)) {
                chrome_.scroll_dragging = true;
                chrome_.scroll_drag_start_y = my;
                chrome_.scroll_drag_start_pos = chrome_.scroll_y;
                chrome_.address_focused = false;
                return;
            }
            chrome_.address_focused = false;

            // Hit test against page content
            if (current_page_.has_value() && current_page_->layout) {
                f32 py = static_cast<f32>(my) - chrome_height() + static_cast<f32>(chrome_.scroll_y);
                auto ht = html::hit_test(current_page_->layout.get(), static_cast<f32>(mx), py);
                if (ht.element) {
                    html::g_form_state.hovered_element = ht.element;
                    std::string tag = ht.element->tag_name;
                    std::string type = ht.element->get_attribute("type");

                    if (tag == "input" && (type.empty() || type == "text")) {
                        html::g_form_state.focus(ht.element);
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
                        html::g_form_state.focus(ht.element);
                    } else if (tag == "select") {
                        html::g_form_state.focus(ht.element);
                    } else {
                        html::g_form_state.blur();
                    }
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
            HWND hwnd = (HWND)window_->get_native_handle();
            IsZoomed(hwnd) ? ShowWindow(hwnd, SW_RESTORE) : ShowWindow(hwnd, SW_MAXIMIZE);
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

        f32 tab_start = ChromeUI::PADDING;
        for (u32 i = 0; i < chrome_.tabs.size(); i++) {
            if (mx >= tab_start + i * (ChromeUI::TAB_W + 2) &&
                mx <= tab_start + i * (ChromeUI::TAB_W + 2) + ChromeUI::TAB_W) {
                if (chrome_.active_tab != i) {
                    chrome_.tabs[chrome_.active_tab].scroll_y = chrome_.scroll_y;
                    chrome_.active_tab = i;
                    chrome_.scroll_y = chrome_.tabs[i].scroll_y;
                    start_load(chrome_.tabs[i].url);
                }
                return;
            }
        }

        f32 new_tab_x = tab_start + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
        if (mx >= new_tab_x && mx <= new_tab_x + ChromeUI::NEW_TAB_W) {
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
            refresh();
            return;
        }

        if (is_in_rect(mx, my, r.address)) {
            chrome_.address_focused = true;
            chrome_.edit_buffer = chrome_.url;
            chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.length());
            auto now = std::chrono::steady_clock::now();
            chrome_.blink_start_ms =
                static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            return;
        }

        if (is_in_rect(mx, my, r.download)) {
            chrome_.show_downloads = !chrome_.show_downloads;
            return;
        }

        if (is_in_rect(mx, my, r.bookmark)) {
            handle_bookmark_click();
            return;
        }

        if (is_in_rect(mx, my, r.menu)) {
            chrome_.show_menu = !chrome_.show_menu;
            return;
        }

        if (chrome_.show_menu) {
            f32 mx2 = r.menu.x - 80, my2 = chrome_height();
            f32 my_off = my - my2;
            if (mx >= mx2 && mx <= mx2 + 160 && my_off >= 0 && my_off <= 70) {
                if (my_off < 22)
                    new_tab();
                else if (my_off < 44)
                    chrome_.show_settings = true;
                chrome_.show_menu = false;
                return;
            }
            chrome_.show_menu = false;
        }

        chrome_.address_focused = false;
    }

    void BrowserWindow::handle_key_down(const platform::Event &e) {
        if (e.key == platform::KeyCode::F && chrome_.ctrl_down && chrome_.shift_down) {
            renderer_->toggle_fps_overlay();
            return;
        }
        if (e.key == platform::KeyCode::T && chrome_.ctrl_down) {
            new_tab();
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
            chrome_.find_state.show();
            return;
        }
        if (e.key == platform::KeyCode::G && chrome_.ctrl_down) {
            if (chrome_.find_state.visible && !chrome_.find_state.query.empty()) {
                chrome_.find_state.next();
            }
            return;
        }
        if (e.key == platform::KeyCode::L && chrome_.ctrl_down) {
            chrome_.address_focused = true;
            chrome_.edit_buffer = chrome_.url;
            chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.size());
            chrome_.sel_start = 0;
            chrome_.all_selected = false;
            auto now = std::chrono::steady_clock::now();
            chrome_.blink_start_ms =
                static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            return;
        }
        if (e.key == platform::KeyCode::F12 && !chrome_.ctrl_down) {
            chrome_.devtools.visible = !chrome_.devtools.visible;
            return;
        }
        if (e.key == platform::KeyCode::F11 && !chrome_.ctrl_down) {
            chrome_.fullscreen = !chrome_.fullscreen;
            if (window_) {
                auto *w32 = static_cast<platform::Win32Window*>(window_.get());
                w32->set_fullscreen(chrome_.fullscreen);
            }
            return;
        }
        if (e.key == platform::KeyCode::F6 && !chrome_.ctrl_down) {
            chrome_.address_focused = true;
            chrome_.edit_buffer = chrome_.url;
            chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.size());
            chrome_.sel_start = 0;
            chrome_.all_selected = false;
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
                settings_->save_to_file("./settings.txt");
            }
            return;
        }
        if (e.key == platform::KeyCode::EQUALS && chrome_.ctrl_down) {
            // Ctrl+= (or Ctrl++) zoom in
            if (settings_) {
                f32 z = settings_->zoom_level() * 1.25f;
                if (z > 5.0f) z = 5.0f;
                settings_->set_zoom_level(z);
                settings_->save_to_file("./settings.txt");
            }
            return;
        }
        if (e.key == platform::KeyCode::MINUS && chrome_.ctrl_down) {
            // Ctrl+- zoom out
            if (settings_) {
                f32 z = settings_->zoom_level() / 1.25f;
                if (z < 0.25f) z = 0.25f;
                settings_->set_zoom_level(z);
                settings_->save_to_file("./settings.txt");
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
            if (e.key == platform::KeyCode::W && chrome_.ctrl_down) {
                close_tab(chrome_.active_tab);
                return;
            }
            if (e.key == platform::KeyCode::C && chrome_.ctrl_down) {
                clipboard_copy(chrome_.edit_buffer);
                return;
            }
            if (e.key == platform::KeyCode::X && chrome_.ctrl_down) {
                clipboard_copy(chrome_.edit_buffer);
                chrome_.edit_buffer.clear();
                chrome_.cursor_pos = 0;
                return;
            }
            if (e.key == platform::KeyCode::V && chrome_.ctrl_down) {
                std::string paste = clipboard_paste();
                if (!paste.empty()) {
                    if (chrome_.all_selected) {
                        chrome_.edit_buffer = paste;
                        chrome_.cursor_pos = static_cast<u32>(paste.size());
                        chrome_.all_selected = false;
                    } else {
                        chrome_.edit_buffer.insert(chrome_.cursor_pos, paste);
                        chrome_.cursor_pos += static_cast<u32>(paste.size());
                    }
                }
                return;
            }
            if (e.key == platform::KeyCode::A && chrome_.ctrl_down) {
                chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.size());
                chrome_.sel_start = 0;
                chrome_.all_selected = true;
                return;
            }

            if (e.key == platform::KeyCode::ENTER) {
                navigate(chrome_.edit_buffer);
                chrome_.address_focused = false;
                chrome_.edit_buffer.clear();
                chrome_.all_selected = false;
            } else if (e.key == platform::KeyCode::ESCAPE) {
                chrome_.address_focused = false;
                chrome_.edit_buffer.clear();
                chrome_.all_selected = false;
            } else if ((e.key == platform::KeyCode::BACKSPACE)) {
                if (chrome_.all_selected) {
                    chrome_.edit_buffer.clear();
                    chrome_.cursor_pos = 0;
                    chrome_.all_selected = false;
                } else if (chrome_.cursor_pos > 0) {
                    chrome_.edit_buffer.erase(chrome_.cursor_pos - 1, 1);
                    chrome_.cursor_pos--;
                }
            } else if (e.key == platform::KeyCode::DELETE && !chrome_.ctrl_down) {
                if (chrome_.all_selected) {
                    chrome_.edit_buffer.clear();
                    chrome_.cursor_pos = 0;
                    chrome_.all_selected = false;
                } else if (chrome_.cursor_pos < chrome_.edit_buffer.length()) {
                    chrome_.edit_buffer.erase(chrome_.cursor_pos, 1);
                }
            } else if (e.key == platform::KeyCode::LEFT && !chrome_.ctrl_down) {
                if (chrome_.cursor_pos > 0)
                    chrome_.cursor_pos--;
                chrome_.all_selected = false;
            } else if (e.key == platform::KeyCode::RIGHT && !chrome_.ctrl_down) {
                if (chrome_.cursor_pos < chrome_.edit_buffer.length())
                    chrome_.cursor_pos++;
                chrome_.all_selected = false;
            } else if (e.key == platform::KeyCode::HOME) {
                chrome_.cursor_pos = 0;
                chrome_.all_selected = false;
            } else if (e.key == platform::KeyCode::END) {
                chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.length());
                chrome_.all_selected = false;
            } else {
                char c = keycode_to_char(e.key, chrome_.shift_down);
                if (c) {
                    if (chrome_.all_selected) {
                        chrome_.edit_buffer.clear();
                        chrome_.cursor_pos = 0;
                        chrome_.all_selected = false;
                    }
                    chrome_.edit_buffer.insert(chrome_.cursor_pos, 1, c);
                    chrome_.cursor_pos++;
                }
            }
        } else if (html::g_form_state.focused_element) {
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

            if (tag == "input" && (type.empty() || type == "text")) {
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
                } else if (e.key == platform::KeyCode::HOME) {
                    html::g_form_state.caret_position = 0;
                } else if (e.key == platform::KeyCode::END) {
                    std::string val = html::g_form_state.get_value(el);
                    html::g_form_state.caret_position = static_cast<u32>(val.size());
                } else if (e.key == platform::KeyCode::V && chrome_.ctrl_down) {
                    std::string paste = clipboard_paste();
                    if (!paste.empty()) {
                        std::string val = html::g_form_state.get_value(el);
                        val.insert(html::g_form_state.caret_position, paste);
                        html::g_form_state.caret_position += static_cast<u32>(paste.size());
                        html::g_form_state.set_value(el, val);
                    }
                } else if (e.key == platform::KeyCode::C && chrome_.ctrl_down) {
                    std::string val = html::g_form_state.get_value(el);
                    clipboard_copy(val);
                } else {
                    char c = keycode_to_char(e.key, chrome_.shift_down);
                    if (c) {
                        std::string val = html::g_form_state.get_value(el);
                        val.insert(html::g_form_state.caret_position, 1, c);
                        html::g_form_state.caret_position++;
                        html::g_form_state.set_value(el, val);
                    }
                }
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
        chrome_.hovered_button = -1;
        chrome_.hovered_tab = -1;
        chrome_.hovered_close = -1;
        if (my > chrome_height()) {
            update_tab_tooltip(-1, -1);
            return;
        }
        update_tab_tooltip(mx, my);

        for (u32 i = 0; i < chrome_.rects.tab_close.size(); i++) {
            if (is_in_rect(mx, my, chrome_.rects.tab_close[i])) {
                chrome_.hovered_close = static_cast<i32>(i);
                return;
            }
        }

        f32 tab_start = ChromeUI::PADDING;
        for (u32 i = 0; i < chrome_.tabs.size(); i++) {
            if (mx >= tab_start + i * (ChromeUI::TAB_W + 2) &&
                mx <= tab_start + i * (ChromeUI::TAB_W + 2) + ChromeUI::TAB_W) {
                chrome_.hovered_tab = static_cast<i32>(i);
                return;
            }
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
            chrome_.hovered_button = ChromeUI::BOOKMARK + 1;
        else if (is_in_rect(mx, my, r.bookmark))
            chrome_.hovered_button = ChromeUI::BOOKMARK;
        else if (is_in_rect(mx, my, r.menu))
            chrome_.hovered_button = ChromeUI::MENU;

        f32 ntx = tab_start + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
        if (mx >= ntx && mx <= ntx + ChromeUI::NEW_TAB_W)
            chrome_.hovered_button = ChromeUI::REFRESH + 1;
    }

    void BrowserWindow::handle_scroll(i32 delta) {
        if (compositor_enabled_ && compositor_) {
            f32 dy = static_cast<f32>(-delta * 30);
            compositor_->set_root_scroll_delta(dy);
        } else {
            chrome_.scroll_y =
                std::max(0, std::min(chrome_.scroll_max, static_cast<i32>(chrome_.scroll_y - delta * 30)));
        }
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
        bookmarks_->save_to_file("./bookmarks.txt");
        chrome_.is_bookmarked = bookmarks_->is_bookmarked(chrome_.url);
    }

}  // namespace browser
