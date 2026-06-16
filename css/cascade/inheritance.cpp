#include "engine.hpp"

#include <string>

namespace browser::css {

    bool is_inherited(const std::string &property) {
        return property == "color" || property == "font-size" || property == "font-family" ||
               property == "font-weight" || property == "font-style" || property == "line-height" ||
               property == "visibility" || property == "cursor" || property == "text-align" ||
               property == "white-space" || property == "word-break" || property == "overflow-wrap";
    }

}  // namespace browser::css
