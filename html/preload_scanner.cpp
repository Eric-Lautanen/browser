#include "preload_scanner.hpp"

#include <cctype>

namespace browser::html {

    void PreloadScanner::set_fetch_callback(FetchCallback cb) {
        fetch_cb_ = std::move(cb);
    }

    void PreloadScanner::scan_token(const Token &token, const std::string &base_url) {
        if (token.index() != 1)
            return;  // Only process TagToken
        const auto &tag = std::get<TagToken>(token);
        if (tag.type != TokenType::START_TAG)
            return;

        auto get_attr = [&](const std::string &name) -> std::string {
            for (const auto &attr : tag.attributes) {
                std::string attr_name_lower;
                for (char c : attr.name)
                    attr_name_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (attr_name_lower == name)
                    return attr.value;
            }
            return {};
        };

        auto resolve_url = [&](const std::string &url) -> std::string {
            if (url.empty())
                return {};
            if (url.find("://") != std::string::npos || url.find("//") == 0)
                return url;
            if (url[0] == '/') {
                // Absolute path: resolve against base URL origin
                auto pos = base_url.find("://");
                if (pos == std::string::npos)
                    return url;
                auto slash = base_url.find('/', pos + 3);
                if (slash == std::string::npos)
                    return base_url + url;
                return base_url.substr(0, slash) + url;
            }
            // Relative: append to base directory and resolve . and .. segments
            auto last_slash = base_url.rfind('/');
            if (last_slash == std::string::npos || last_slash < base_url.find("://") + 2) {
                return base_url + "/" + url;
            }
            std::string resolved = base_url.substr(0, last_slash + 1) + url;
            // Collapse .. and . path segments
            std::string collapsed;
            std::vector<std::string> segments;
            std::string seg;
            for (char c : resolved) {
                if (c == '/') {
                    if (!seg.empty()) {
                        if (seg == "..") {
                            if (!segments.empty())
                                segments.pop_back();
                        } else if (seg != ".") {
                            segments.push_back(seg);
                        }
                        seg.clear();
                    }
                    collapsed += '/';
                } else {
                    seg += c;
                }
            }
            if (!seg.empty()) {
                if (seg == "..") {
                    if (!segments.empty())
                        segments.pop_back();
                } else if (seg != ".") {
                    segments.push_back(seg);
                }
            }
            // Rebuild the URL preserving scheme+authority
            auto proto = resolved.find("://");
            if (proto != std::string::npos) {
                auto auth_end = resolved.find('/', proto + 3);
                if (auth_end != std::string::npos) {
                    collapsed = resolved.substr(0, auth_end + 1);
                } else {
                    collapsed = resolved + "/";
                }
            } else {
                collapsed = "/";
            }
            for (auto &s : segments) {
                collapsed += s + "/";
            }
            if (!collapsed.empty() && collapsed.back() == '/' && resolved.back() != '/') {
                collapsed.pop_back();
            }
            return collapsed;
        };

        PreloadRequest req;

        if (tag.tag_name == "img") {
            std::string src = get_attr("src");
            if (!src.empty()) {
                req.url = resolve_url(src);
                req.as = "image";
            }
        } else if (tag.tag_name == "link") {
            std::string rel = get_attr("rel");
            if (rel == "stylesheet") {
                std::string href = get_attr("href");
                if (!href.empty()) {
                    req.url = resolve_url(href);
                    req.as = "style";
                }
            } else if (rel == "preload") {
                std::string href = get_attr("href");
                if (!href.empty()) {
                    req.url = resolve_url(href);
                    req.as = get_attr("as");
                    if (req.as.empty())
                        req.as = "fetch";
                }
            }
        } else if (tag.tag_name == "script") {
            std::string src = get_attr("src");
            if (!src.empty()) {
                req.url = resolve_url(src);
                req.as = "script";
                std::string async_attr = get_attr("async");
                std::string defer_attr = get_attr("defer");
                std::string type_attr = get_attr("type");
                req.is_async = !async_attr.empty();
                req.is_defer = !defer_attr.empty();
                req.is_module = (type_attr == "module");
            }
        }

        if (!req.url.empty()) {
            pending_.push_back(req);
            if (fetch_cb_) {
                fetch_cb_(req);
            }
        }
    }

    const std::vector<PreloadRequest> &PreloadScanner::pending() const {
        return pending_;
    }

    void PreloadScanner::clear_pending() {
        pending_.clear();
    }

}  // namespace browser::html
