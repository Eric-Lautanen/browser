#include "font_loader.hpp"
#include "../net/http_client.hpp"
#include "../net/url.hpp"
#include <algorithm>
#include <cctype>

namespace browser::render {

FontLoader::FontLoader(FontManager* fm, net::HTTPClient* http)
    : fm_(fm), http_(http) {}

void FontLoader::load_font_face(const css::AtRule& at_rule) {
    if (at_rule.name != "font-face") return;

    std::string font_family;
    std::string src_url;
    std::string font_weight = "normal";
    std::string font_style = "normal";
    std::string unicode_range;

    for (const auto& decl : at_rule.declarations) {
        std::string prop;
        for (char c : decl.property) {
            prop += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (prop == "font-family") {
            if (!decl.values.empty()) {
                font_family = decl.values[0].string_value;
                if (font_family.empty()) font_family = decl.values[0].keyword;
            }
        } else if (prop == "src") {
            for (const auto& val : decl.values) {
                if (val.type == css::CSSValue::Type::URL) {
                    src_url = val.string_value;
                    break;
                }
            }
        } else if (prop == "font-weight") {
            if (!decl.values.empty()) font_weight = decl.values[0].keyword;
        } else if (prop == "font-style") {
            if (!decl.values.empty()) font_style = decl.values[0].keyword;
        } else if (prop == "unicode-range") {
            if (!decl.values.empty()) unicode_range = decl.values[0].string_value;
        }
    }

    if (font_family.empty() || src_url.empty()) return;

    FontFaceRequest req;
    req.font_family = font_family;
    req.src_url = src_url;
    req.font_weight = font_weight;
    req.font_style = font_style;
    req.unicode_range = unicode_range;

    pending_.push_back(std::move(req));
}

void FontLoader::load_from_stylesheet(const css::StyleSheet& sheet) {
    for (const auto& at : sheet.at_rules) {
        if (at.name == "font-face") {
            load_font_face(at);
        }
    }
}

void FontLoader::load_from_at_rules(const std::vector<css::AtRule>& rules) {
    for (const auto& at : rules) {
        if (at.name == "font-face") {
            load_font_face(at);
        }
    }
}

async::task<bool> FontLoader::fetch_all(const std::string& base_url) {
    for (auto& req : pending_) {
        std::string url = req.src_url;
        // Resolve relative URL
        if (url.find("://") == std::string::npos && url.find("//") != 0) {
            if (url[0] == '/') {
                auto pos = base_url.find("://");
                if (pos != std::string::npos) {
                    auto slash = base_url.find('/', pos + 3);
                    if (slash != std::string::npos) {
                        url = base_url.substr(0, slash) + url;
                    } else {
                        url = base_url + url;
                    }
                }
            } else {
                auto last_slash = base_url.rfind('/');
                if (last_slash != std::string::npos && last_slash > base_url.find("://") + 2) {
                    url = base_url.substr(0, last_slash + 1) + url;
                } else {
                    url = base_url + "/" + url;
                }
            }
        }

        auto parsed = net::URL::parse(url);
        if (parsed.is_err()) continue;

        net::http::Request http_req;
        http_req.method = net::http::Method::GET;
        http_req.url = parsed.unwrap();
        {
            std::string host_hdr = http_req.url.host;
            if (http_req.url.port != 0 && http_req.url.port != http_req.url.default_port())
                host_hdr += ":" + std::to_string(http_req.url.port);
            http_req.headers.set("Host", host_hdr);
        }
        http_req.headers.set("User-Agent", "Browser/0.1");
        http_req.headers.set("Accept", "font/*");

        auto resp_r = http_->fetch_async(http_req).sync_wait();
        if (resp_r.is_err()) continue;

        auto resp = std::move(resp_r.unwrap());
        if (resp.body.empty()) continue;

        // TTF/WOFF/WOFF2 auto-detection and loading
        auto* face = fm_->load_from_memory(resp.body.data(), static_cast<u32>(resp.body.size()));
        if (face) {
            font_faces_[req.font_family] = face;
        }
    }

    pending_.clear();
    co_return true;
}

FontFace* FontLoader::get_font_face(const std::string& font_family) const {
    auto it = font_faces_.find(font_family);
    return (it != font_faces_.end()) ? it->second : nullptr;
}

bool FontLoader::has_font(const std::string& font_family) const {
    return font_faces_.find(font_family) != font_faces_.end();
}

} // namespace browser::render
