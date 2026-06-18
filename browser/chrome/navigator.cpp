#include "../../html/form_submission.hpp"
#include "../../net/url.hpp"
#include "../history.hpp"
#include "../page_loader.hpp"
#include "../settings.hpp"
#include "window.hpp"

namespace browser {

    void BrowserWindow::navigate(const std::string &url) {
        std::string final_url = url;
        while (!final_url.empty() && (final_url[0] == ' ' || final_url[0] == '\t')) final_url.erase(final_url.begin());
        while (!final_url.empty() && (final_url.back() == ' ' || final_url.back() == '\t')) final_url.pop_back();

        if (final_url.empty())
            return;
        bool looks_like_url = (final_url.find("://") != std::string::npos ||
                               (final_url.find('.') != std::string::npos && final_url.find(' ') == std::string::npos &&
                                final_url.find('\t') == std::string::npos) ||
                               final_url.rfind("about:", 0) == 0 || final_url.rfind("view-source:", 0) == 0 ||
                               final_url.rfind("file:", 0) == 0);
        if (looks_like_url) {
            if (final_url.find("://") == std::string::npos && final_url.rfind("about:", 0) != 0 &&
                final_url.rfind("view-source:", 0) != 0 && final_url.rfind("file:", 0) != 0) {
                final_url = "https://" + final_url;
            }
        } else {
            std::string search_engine_url = "https://www.google.com/search?q=";
            if (settings_) {
                auto se = settings_->search_engine();
                if (se == "bing")
                    search_engine_url = "https://www.bing.com/search?q=";
                else if (se == "duckduckgo")
                    search_engine_url = "https://duckduckgo.com/?q=";
            }
            final_url = search_engine_url + html::url_encode(final_url);
        }

        chrome_.url = final_url;
        if (!chrome_.tabs.empty()) {
            chrome_.tabs[chrome_.active_tab].url = final_url;
            update_tab_placeholder(chrome_.active_tab);
        }
        start_load(final_url);
        if (!chrome_.tabs.empty() && chrome_.tabs[chrome_.active_tab].history)
            chrome_.tabs[chrome_.active_tab].history->push(final_url, "");
    }

    void BrowserWindow::navigate_back() {
        if (chrome_.tabs.empty())
            return;
        auto &tab = chrome_.tabs[chrome_.active_tab];
        if (!tab.history)
            return;
        auto url = tab.history->go_back();
        if (url.has_value()) {
            chrome_.url = url.value();
            chrome_.tabs[chrome_.active_tab].url = url.value();
            update_tab_placeholder(chrome_.active_tab);
            start_load(url.value());
        }
    }

    void BrowserWindow::navigate_forward() {
        if (chrome_.tabs.empty())
            return;
        auto &tab = chrome_.tabs[chrome_.active_tab];
        if (!tab.history)
            return;
        auto url = tab.history->go_forward();
        if (url.has_value()) {
            chrome_.url = url.value();
            chrome_.tabs[chrome_.active_tab].url = url.value();
            update_tab_placeholder(chrome_.active_tab);
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
        tab.history = std::make_unique<HistoryManager>();
        chrome_.tabs.push_back(std::move(tab));
        chrome_.active_tab = static_cast<u32>(chrome_.tabs.size()) - 1;
        compute_layout();
        update_tab_placeholder(chrome_.active_tab);
        chrome_.url = url;
        start_load(url);
        if (!chrome_.tabs.empty() && chrome_.tabs[chrome_.active_tab].history)
            chrome_.tabs[chrome_.active_tab].history->push(url, "");
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
