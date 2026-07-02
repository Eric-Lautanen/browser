#include "parser.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace browser::css {
    namespace {

        struct ShorthandEntry {
            const char *shorthand;
            const char *longhands[12];  // null-terminated array
        };

        // Shorthand to longhand expansion maps
        const ShorthandEntry kShorthandTable[] = {
            // Box model
            {"margin", {"margin-top", "margin-right", "margin-bottom", "margin-left", nullptr}},
            {"padding", {"padding-top", "padding-right", "padding-bottom", "padding-left", nullptr}},
            {"border",
             {"border-top-width",
              "border-right-width",
              "border-bottom-width",
              "border-left-width",
              "border-top-color",
              "border-right-color",
              "border-bottom-color",
              "border-left-color",
              nullptr}},
            {"border-top", {"border-top-width", "border-top-style", "border-top-color", nullptr}},
            {"border-right", {"border-right-width", "border-right-style", "border-right-color", nullptr}},
            {"border-bottom", {"border-bottom-width", "border-bottom-style", "border-bottom-color", nullptr}},
            {"border-left", {"border-left-width", "border-left-style", "border-left-color", nullptr}},
            {"border-width",
             {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width", nullptr}},
            {"border-color",
             {"border-top-color", "border-right-color", "border-bottom-color", "border-left-color", nullptr}},
            {"border-style",
             {"border-top-style", "border-right-style", "border-bottom-style", "border-left-style", nullptr}},
            {"border-radius",
             {"border-top-left-radius",
              "border-top-right-radius",
              "border-bottom-right-radius",
              "border-bottom-left-radius",
              nullptr}},

            // Background
            {"background",
             {"background-color",
              "background-image",
              "background-repeat",
              "background-position",
              "background-size",
              nullptr}},

            // Font
            {"font", {"font-style", "font-variant", "font-weight", "font-size", "line-height", "font-family", nullptr}},

            // Text
            {"text-decoration", {"text-decoration-line", "text-decoration-color", "text-decoration-style", nullptr}},
            {"text-emphasis", {"text-emphasis-style", "text-emphasis-color", nullptr}},

            // Outline
            {"outline", {"outline-width", "outline-style", "outline-color", nullptr}},

            // Overflow
            {"overflow", {"overflow-x", "overflow-y", nullptr}},

            // Flexbox
            {"flex", {"flex-grow", "flex-shrink", "flex-basis", nullptr}},
            {"flex-flow", {"flex-direction", "flex-wrap", nullptr}},

            // Grid
            {"grid-template",
             {"grid-template-rows", "grid-template-columns", "grid-template-areas", nullptr}},
            {"grid",
             {"grid-template-rows",
              "grid-template-columns",
              "grid-template-areas",
              "grid-auto-rows",
              "grid-auto-columns",
              "grid-auto-flow",
              nullptr}},
            {"grid-row", {"grid-row-start", "grid-row-end", nullptr}},
            {"grid-column", {"grid-column-start", "grid-column-end", nullptr}},
            {"grid-area",
             {"grid-row-start", "grid-row-end", "grid-column-start", "grid-column-end", nullptr}},
            {"gap", {"row-gap", "column-gap", nullptr}},
            {"grid-gap", {"grid-row-gap", "grid-column-gap", nullptr}},

            // Place (alignment shorthands)
            {"place-content", {"align-content", "justify-content", nullptr}},
            {"place-items", {"align-items", "justify-items", nullptr}},
            {"place-self", {"align-self", "justify-self", nullptr}},

            // Animation / Transition
            {"animation",
             {"animation-name",
              "animation-duration",
              "animation-timing-function",
              "animation-delay",
              "animation-iteration-count",
              "animation-direction",
              "animation-fill-mode",
              nullptr}},
            {"transition",
             {"transition-property",
              "transition-duration",
              "transition-timing-function",
              "transition-delay",
              nullptr}},

            // List
            {"list-style", {"list-style-type", "list-style-position", "list-style-image", nullptr}},

            // Columns
            {"columns", {"column-width", "column-count", nullptr}},
            {"column-rule", {"column-rule-width", "column-rule-style", "column-rule-color", nullptr}},

            // Page break
            {"page-break-before", {"break-before", nullptr}},
            {"page-break-after", {"break-after", nullptr}},
            {"page-break-inside", {"break-inside", nullptr}},

            // Offset (positioning)
            {"offset", {"offset-path", "offset-distance", "offset-rotate", nullptr}},

            // Mask
            {"mask",
             {"mask-image",
              "mask-mode",
              "mask-position",
              "mask-size",
              "mask-repeat",
              "mask-origin",
              "mask-clip",
              nullptr}},

            // Scroll margin/padding
            {"scroll-margin",
             {"scroll-margin-top", "scroll-margin-right", "scroll-margin-bottom", "scroll-margin-left", nullptr}},
            {"scroll-padding",
             {"scroll-padding-top", "scroll-padding-right", "scroll-padding-bottom", "scroll-padding-left", nullptr}},

            // Inset (logical positioning)
            {"inset", {"top", "right", "bottom", "left", nullptr}},

            // Margin/padding block/inline (logical)
            {"margin-block", {"margin-block-start", "margin-block-end", nullptr}},
            {"margin-inline", {"margin-inline-start", "margin-inline-end", nullptr}},
            {"padding-block", {"padding-block-start", "padding-block-end", nullptr}},
            {"padding-inline", {"padding-inline-start", "padding-inline-end", nullptr}},

            // Border block/inline (logical)
            {"border-block-width", {"border-block-start-width", "border-block-end-width", nullptr}},
            {"border-inline-width", {"border-inline-start-width", "border-inline-end-width", nullptr}},
            {"border-block-color", {"border-block-start-color", "border-block-end-color", nullptr}},
            {"border-inline-color", {"border-inline-start-color", "border-inline-end-color", nullptr}},
            {"border-block-style", {"border-block-start-style", "border-block-end-style", nullptr}},
            {"border-inline-style", {"border-inline-start-style", "border-inline-end-style", nullptr}},

            // Border radius (logical)
            {"border-start-start-radius", {"border-top-left-radius", nullptr}},
            {"border-start-end-radius", {"border-top-right-radius", nullptr}},
            {"border-end-start-radius", {"border-bottom-left-radius", nullptr}},
            {"border-end-end-radius", {"border-bottom-right-radius", nullptr}},

            // Container
            {"container", {"container-type", "container-name", nullptr}},

            // Text wrap
            {"text-wrap", {"text-wrap-mode", "text-wrap-style", nullptr}},

            // Font synthesis
            {"font-synthesis", {"font-synthesis-weight", "font-synthesis-style", "font-synthesis-small-caps", nullptr}},

            // Text stroke (WebKit)
            {"-webkit-text-stroke", {"-webkit-text-stroke-width", "-webkit-text-stroke-color", nullptr}},

            // Box decoration break
            // (single property, no shorthand needed)

            {nullptr, {nullptr}}  // sentinel
        };

        bool iequals(const std::string &a, const std::string &b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        }

    }  // anonymous namespace

    bool is_shorthand_property(const std::string &name) {
        for (int i = 0; kShorthandTable[i].shorthand != nullptr; i++) {
            if (iequals(name, kShorthandTable[i].shorthand))
                return true;
        }
        return false;
    }

    std::vector<std::string> expand_shorthand(const std::string &name) {
        for (int i = 0; kShorthandTable[i].shorthand != nullptr; i++) {
            if (iequals(name, kShorthandTable[i].shorthand)) {
                std::vector<std::string> result;
                for (int j = 0; kShorthandTable[i].longhands[j] != nullptr; j++) {
                    result.push_back(kShorthandTable[i].longhands[j]);
                }
                return result;
            }
        }
        return {};
    }

}  // namespace browser::css
