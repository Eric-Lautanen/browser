#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "font.hpp"
#include "../css/css_values.hpp"
#include "../async/task.hpp"

namespace browser::net { class HTTPClient; }

namespace browser::render {

struct FontFaceRequest {
    std::string font_family;
    std::string src_url;
    std::string font_weight;
    std::string font_style;
    std::string unicode_range;
};

class FontLoader {
public:
    FontLoader(FontManager* fm, net::HTTPClient* http);

    void load_font_face(const css::AtRule& at_rule);
    void load_from_stylesheet(const css::StyleSheet& sheet);
    void load_from_at_rules(const std::vector<css::AtRule>& rules);
    async::task<bool> fetch_all(const std::string& base_url);

    FontFace* get_font_face(const std::string& font_family) const;
    bool has_font(const std::string& font_family) const;

private:
    FontManager* fm_;
    net::HTTPClient* http_;
    std::vector<FontFaceRequest> pending_;
    std::unordered_map<std::string, FontFace*> font_faces_;
};

} // namespace browser::render
