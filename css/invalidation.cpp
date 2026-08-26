#include "invalidation.hpp"

#include <algorithm>
#include <cstddef>

namespace browser::css {

    namespace {

        bool transforms_equal(const CSSValue &a, const CSSValue &b) {
            if (a.transforms.size() != b.transforms.size())
                return false;
            for (size_t i = 0; i < a.transforms.size(); i++) {
                const TransformFunc &ta = a.transforms[i];
                const TransformFunc &tb = b.transforms[i];
                if (ta.type != tb.type || ta.args.size() != tb.args.size())
                    return false;
                for (size_t j = 0; j < ta.args.size(); j++) {
                    if (ta.args[j] != tb.args[j])
                        return false;
                }
            }
            return true;
        }

    }  // namespace

    bool css_values_equal(const CSSValue &a, const CSSValue &b) {
        if (a.type != b.type)
            return false;
        switch (a.type) {
            case CSSValue::Type::KEYWORD:
                return a.keyword == b.keyword;
            case CSSValue::Type::LENGTH:
                return a.length.value == b.length.value && a.length.unit == b.length.unit;
            case CSSValue::Type::COLOR:
                return a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b &&
                       a.color.a == b.color.a;
            case CSSValue::Type::NUMBER:
            case CSSValue::Type::PERCENTAGE:
                return a.number == b.number;
            case CSSValue::Type::STRING:
            case CSSValue::Type::URL:
                return a.string_value == b.string_value;
            case CSSValue::Type::TRANSFORM:
                return transforms_equal(a, b);
            // Composite payloads (gradients, shadow/filter/transition lists)
            // are not animated in practice; treat any difference as changed.
            default:
                return false;
        }
    }

    StyleImpact style_change_impact(const std::string &property) {
        // Properties the painter reads directly from LayoutNode styles whose
        // values cannot change any box geometry. Keep this list conservative:
        // when in doubt a property is Layout.
        static const char *const kPaintOnly[] = {
            "opacity",
            "transform",
            "color",
            "background-color",
            "border-top-color",
            "border-right-color",
            "border-bottom-color",
            "border-left-color",
            "outline-color",
            "box-shadow",
            "text-shadow",
            "text-decoration-color",
            "column-rule-color",
            "background-image",
        };
        for (const char *p : kPaintOnly) {
            if (property == p)
                return StyleImpact::PaintOnly;
        }
        return StyleImpact::Layout;
    }

}  // namespace browser::css
