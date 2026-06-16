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
            {"border-top", {"border-top-width", "border-top-color", nullptr}},
            {"border-right", {"border-right-width", "border-right-color", nullptr}},
            {"border-bottom", {"border-bottom-width", "border-bottom-color", nullptr}},
            {"border-left", {"border-left-width", "border-left-color", nullptr}},
            {"border-width",
             {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width", nullptr}},
            {"border-color",
             {"border-top-color", "border-right-color", "border-bottom-color", "border-left-color", nullptr}},
            {"border-style",
             {"border-top-style", "border-right-style", "border-bottom-style", "border-left-style", nullptr}},
            {"background",
             {"background-color",
              "background-image",
              "background-repeat",
              "background-position",
              "background-size",
              nullptr}},
            {"font", {"font-style", "font-variant", "font-weight", "font-size", "line-height", "font-family", nullptr}},
            {"flex", {"flex-grow", "flex-shrink", "flex-basis", nullptr}},
            {"animation",
             {"animation-name",
              "animation-duration",
              "animation-timing-function",
              "animation-delay",
              "animation-iteration-count",
              "animation-direction",
              "animation-fill-mode",
              nullptr}},
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
