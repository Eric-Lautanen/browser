#pragma once
#include <unordered_map>
#include "css_values.hpp"
#include "specificity.hpp"
#include "../html/dom.hpp"
#include "../async/task.hpp"

namespace browser::css {

bool is_inherited(const std::string& property);

struct MatchedDecl {
    const Declaration* decl;
    Specificity specificity;
    u32 source_order;
    u8 origin;  // 0=UA, 1=author, 2=inline
    std::string pseudo_element; // empty for main element, "before"/"after"/etc for pseudo-elements
};

struct ComputedStyle {
    std::unordered_map<std::string, CSSValue> properties;
    std::unordered_map<std::string, ComputedStyle> pseudo_styles; // key: "before", "after", etc.
    const ComputedStyle* parent = nullptr;

    bool has(const std::string& p) const {
        return properties.find(p) != properties.end();
    }

    const CSSValue* get(const std::string& p) const {
        auto it = properties.find(p);
        if (it != properties.end()) return &it->second;
        if (parent && is_inherited(p)) return parent->get(p);
        return nullptr;
    }
};

class Cascade {
public:
    struct CascadeResult {
        std::unordered_map<const html::Element*, ComputedStyle> element_styles;
    };

    async::task<CascadeResult>
    compute_async(const html::Document& doc, const StyleSheet& author,
                  f32 viewport_width = 800, f32 viewport_height = 600,
                  f32 device_pixel_ratio = 1.0f,
                  const std::string& color_scheme = "light");
};

}
