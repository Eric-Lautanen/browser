#include "find_bar.hpp"

#include "../html/traversal.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace browser {

namespace {

bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); i++) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); j++) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

}  // namespace

void FindState::search(const html::Document* doc, const std::string& q) {
    matches.clear();
    current_match = 0;
    query = q;

    if (q.empty() || !doc) return;

    html::traverse_depth_first(const_cast<html::Document*>(doc), [&](html::Node* node) {
        if (node->type != html::NodeType::ELEMENT) return;
        auto* el = static_cast<html::Element*>(node);
        std::string text = html::inner_text(el);
        if (icontains(text, q)) {
            matches.push_back(el);
        }
    });
}

void FindState::next() {
    if (matches.empty()) return;
    current_match = (current_match + 1) % static_cast<u32>(matches.size());
}

void FindState::previous() {
    if (matches.empty()) return;
    if (current_match == 0) current_match = static_cast<u32>(matches.size()) - 1;
    else current_match--;
}

void FindState::clear() {
    query.clear();
    matches.clear();
    current_match = 0;
}

}  // namespace browser
