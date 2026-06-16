#pragma once
#include "../tests/utility.hpp"

namespace browser::html {

struct NamedEntity {
    const char* name;
    char32_t codepoint;
};

extern const NamedEntity HTML_ENTITIES[];
extern const u32 HTML_ENTITIES_COUNT;

} // namespace browser::html
