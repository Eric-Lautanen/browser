#pragma once
#include "../html/dom.hpp"
#include "../tests/utility.hpp"

#include <string>
#include <vector>

namespace browser {

struct FindState {
    std::string query;
    std::vector<const html::Element*> matches;
    u32 current_match = 0;
    bool visible = false;

    void search(const html::Document* doc, const std::string& q);
    void next();
    void previous();
    void clear();
    void show() { visible = true; query.clear(); current_match = 0; matches.clear(); }
    void hide() { visible = false; clear(); }
};

}  // namespace browser
