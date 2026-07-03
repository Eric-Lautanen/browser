#include "engine.hpp"

#include <string>

namespace browser::css {

    bool is_inherited(const std::string &property) {
        // CSS Inherited properties — per spec
        // Text & font
        if (property == "color" || property == "font-size" || property == "font-family" ||
            property == "font-weight" || property == "font-style" || property == "font-variant" ||
            property == "font" || property == "line-height" ||
            property == "letter-spacing" || property == "word-spacing" ||
            property == "text-indent" || property == "text-align" || property == "text-transform" ||
            property == "text-decoration" || property == "text-decoration-line" ||
            property == "text-decoration-color" || property == "text-decoration-style" ||
            property == "text-decoration-thickness" || property == "text-underline-offset" ||
            property == "text-shadow" ||
            property == "white-space" || property == "word-break" || property == "overflow-wrap" ||
            property == "direction" || property == "unicode-bidi" || property == "writing-mode" ||
            property == "hyphens" || property == "tab-size" || property == "text-align-last" ||
            property == "text-justify" || property == "word-wrap" || property == "text-wrap" ||
            property == "text-wrap-mode" || property == "text-wrap-style" || property == "text-overflow" ||
            property == "hanging-punctuation" || property == "line-break")
            return true;

        // Visibility & cursor
        if (property == "visibility" || property == "cursor" || property == "pointer-events" ||
            property == "user-select" || property == "accent-color" || property == "caret-color")
            return true;

        // List
        if (property == "list-style-type" || property == "list-style-position" ||
            property == "list-style-image" || property == "list-style")
            return true;

        // Table
        if (property == "border-collapse" || property == "border-spacing" ||
            property == "caption-side" || property == "empty-cells")
            return true;

        // Print
        if (property == "orphans" || property == "widows" || property == "page-break-inside" ||
            property == "break-inside")
            return true;

        // Quotes
        if (property == "quotes")
            return true;

        // Image rendering
        if (property == "image-rendering")
            return true;

        // Font feature settings
        if (property == "font-feature-settings" || property == "font-variant-ligatures" ||
            property == "font-variant-caps" || property == "font-variant-numeric" ||
            property == "font-variant-east-asian" || property == "font-variant-alternates" ||
            property == "font-language-override" || property == "font-kerning" ||
            property == "font-stretch" || property == "font-size-adjust" ||
            property == "font-synthesis" || property == "font-display")
            return true;

        // CSS variables (custom properties) are inherited
        if (property.size() >= 2 && property[0] == '-' && property[1] == '-')
            return true;

        return false;
    }

}  // namespace browser::css
