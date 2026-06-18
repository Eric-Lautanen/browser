#include "engine.hpp"

#include <string>

namespace browser::css {

    bool is_inherited(const std::string &property) {
        return property == "color" || property == "font-size" || property == "font-family" ||
               property == "font-weight" || property == "font-style" || property == "line-height" ||
               property == "visibility" || property == "cursor" || property == "text-align" ||
               property == "white-space" || property == "word-break" || property == "overflow-wrap" ||
               property == "letter-spacing" || property == "text-indent" || property == "text-transform" ||
               property == "direction" || property == "word-spacing" || property == "font-variant" ||
               property == "orphans" || property == "widows";
    }

}  // namespace browser::css
