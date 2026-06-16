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
};

struct ComputedStyle {
    std::unordered_map<std::string, CSSValue> properties;
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
    async::task<std::unordered_map<const html::Element*, ComputedStyle>>
    compute_async(const html::Document& doc, const StyleSheet& author);
};

}
