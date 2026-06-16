#include "../history.hpp"
#include "../page_loader.hpp"
#include "window.hpp"

namespace browser {

    void BrowserWindow::navigate(const std::string &url) {
        chrome_.url = url;
        if (!chrome_.tabs.empty()) {
            chrome_.tabs[chrome_.active_tab].url = url;
            update_tab_placeholder(chrome_.active_tab);
        }
        start_load(url);
        if (history_)
            history_->push(url, "");
    }

    void BrowserWindow::navigate_back() {
        if (!history_)
            return;
        auto url = history_->go_back();
        if (url.has_value()) {
            chrome_.url = url.value();
            if (!chrome_.tabs.empty()) {
                chrome_.tabs[chrome_.active_tab].url = url.value();
                update_tab_placeholder(chrome_.active_tab);
            }
            start_load(url.value());
        }
    }

    void BrowserWindow::navigate_forward() {
        if (!history_)
            return;
        auto url = history_->go_forward();
        if (url.has_value()) {
            chrome_.url = url.value();
            if (!chrome_.tabs.empty()) {
                chrome_.tabs[chrome_.active_tab].url = url.value();
                update_tab_placeholder(chrome_.active_tab);
            }
            start_load(url.value());
        }
    }

    void BrowserWindow::refresh() {
        if (!chrome_.tabs.empty())
            start_load(chrome_.tabs[chrome_.active_tab].url);
    }

    void BrowserWindow::new_tab(const std::string &url) {
        TabInfo tab;
        tab.url = url;
        tab.placeholder_color = {0.7f, 0.7f, 0.7f, 1.0f};
        chrome_.tabs.push_back(tab);
        chrome_.active_tab = static_cast<u32>(chrome_.tabs.size()) - 1;
        compute_layout();
        update_tab_placeholder(chrome_.active_tab);
        chrome_.url = url;
        start_load(url);
        if (history_)
            history_->push(url, "");
    }

    void BrowserWindow::close_tab(u32 index) {
        if (chrome_.tabs.size() <= 1)
            return;
        chrome_.tabs.erase(chrome_.tabs.begin() + static_cast<i64>(index));
        if (chrome_.active_tab >= index && chrome_.active_tab > 0)
            chrome_.active_tab--;
        compute_layout();
        start_load(chrome_.tabs[chrome_.active_tab].url);
    }

}  // namespace browser
