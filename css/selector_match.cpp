#include "selector_match.hpp"
#include <cctype>

namespace browser::css {

namespace {

bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool match_simple(const SimpleSelector& ss, const html::Element* el) {
    switch (ss.type) {
        case SimpleSelector::Type::TAG:
            return iequal(ss.name, el->tag_name);
        case SimpleSelector::Type::UNIVERSAL:
            return true;
        case SimpleSelector::Type::CLASS: {
            auto classes = el->class_list();
            for (const auto& cls : classes) {
                if (cls == ss.name) return true;
            }
            return false;
        }
        case SimpleSelector::Type::ID:
            return el->id() == ss.name;
        case SimpleSelector::Type::ATTRIBUTE: {
            if (ss.match_operator == 0) {
                return el->has_attribute(ss.name);
            }
            std::string val = el->get_attribute(ss.name);
            if (ss.match_operator == '=') {
                return val == ss.value;
            }
            if (ss.match_operator == '~') {
                std::string remaining = val;
                size_t pos = 0;
                while (pos < remaining.size()) {
                    while (pos < remaining.size() && remaining[pos] == ' ') pos++;
                    if (pos >= remaining.size()) break;
                    size_t end = remaining.find(' ', pos);
                    if (end == std::string::npos) end = remaining.size();
                    if (remaining.substr(pos, end - pos) == ss.value) return true;
                    pos = end + 1;
                }
                return false;
            }
            if (ss.match_operator == '|') {
                return val == ss.value || (val.size() > ss.value.size() &&
                    val.substr(0, ss.value.size()) == ss.value &&
                    val[ss.value.size()] == '-');
            }
            if (ss.match_operator == '^') {
                return val.size() >= ss.value.size() &&
                    val.compare(0, ss.value.size(), ss.value) == 0;
            }
            if (ss.match_operator == '$') {
                return val.size() >= ss.value.size() &&
                    val.compare(val.size() - ss.value.size(), ss.value.size(), ss.value) == 0;
            }
            if (ss.match_operator == '*') {
                return val.find(ss.value) != std::string::npos;
            }
            return false;
        }
        case SimpleSelector::Type::PSEUDO_CLASS: {
            // Basic pseudo-class support
            if (ss.name == "first-child") {
                if (!el->parent || el->parent->children.empty()) return false;
                return el->parent->children.front().get() == el;
            }
            if (ss.name == "last-child") {
                if (!el->parent || el->parent->children.empty()) return false;
                return el->parent->children.back().get() == el;
            }
            if (ss.name == "only-child") {
                if (!el->parent) return false;
                u32 count = 0;
                for (auto& c : el->parent->children) {
                    if (c->type == html::NodeType::ELEMENT) count++;
                }
                return count == 1;
            }
            if (ss.name == "root") {
                return el->parent && el->parent->type == html::NodeType::DOCUMENT;
            }
            // All other pseudo-classes (:hover, :focus, :nth-child, :not, etc.) are
            // not supported — return false so they don't incorrectly apply styles.
            return false;
        }
        case SimpleSelector::Type::PSEUDO_ELEMENT:
            // Pseudo-elements select into generated content — not supported
            return false;
    }
    return false;
}

const html::Node* find_previous_sibling(const html::Node* node) {
    if (!node || !node->parent) return nullptr;
    for (size_t i = 0; i < node->parent->children.size(); i++) {
        if (node->parent->children[i].get() == node) {
            if (i == 0) return nullptr;
            return node->parent->children[i - 1].get();
        }
    }
    return nullptr;
}

} // anonymous namespace

bool matches_compound(const std::vector<SimpleSelector>& compound, const html::Element* el) {
    if (!el) return false;
    for (const auto& ss : compound) {
        if (!match_simple(ss, el)) return false;
    }
    return true;
}

bool matches_selector(const Selector& sel, const html::Element* el, const html::Node* root) {
    if (sel.compounds.empty() || !el) return false;

    const html::Element* current = el;
    int c = static_cast<int>(sel.compounds.size()) - 1;

    if (!matches_compound(sel.compounds[c].simples, current)) return false;
    c--;

    while (c >= 0 && current) {
        auto comb = sel.combinators[c];

        if (comb == Combinator::CHILD) {
            if (!current->parent || current->parent->type != html::NodeType::ELEMENT) return false;
            auto* parent = static_cast<const html::Element*>(current->parent);
            if (!matches_compound(sel.compounds[c].simples, parent)) return false;
            current = parent;
        } else if (comb == Combinator::DESCENDANT) {
            bool found = false;
            const html::Node* ancestor = current->parent;
            while (ancestor && ancestor != root) {
                if (ancestor->type == html::NodeType::ELEMENT) {
                    auto* ae = static_cast<const html::Element*>(ancestor);
                    if (matches_compound(sel.compounds[c].simples, ae)) {
                        current = ae;
                        found = true;
                        break;
                    }
                }
                ancestor = ancestor->parent;
            }
            if (!found) return false;
        } else if (comb == Combinator::ADJACENT_SIBLING) {
            auto* prev = find_previous_sibling(current);
            if (!prev || prev->type != html::NodeType::ELEMENT) return false;
            auto* pe = static_cast<const html::Element*>(prev);
            if (!matches_compound(sel.compounds[c].simples, pe)) return false;
            current = pe;
        } else if (comb == Combinator::GENERAL_SIBLING) {
            bool found = false;
            if (!current->parent) return false;
            for (size_t i = 0; i < current->parent->children.size(); i++) {
                if (current->parent->children[i].get() == current) {
                    for (int j = static_cast<int>(i) - 1; j >= 0; j--) {
                        auto* sibling = current->parent->children[j].get();
                        if (sibling->type == html::NodeType::ELEMENT) {
                            auto* se = static_cast<const html::Element*>(sibling);
                            if (matches_compound(sel.compounds[c].simples, se)) {
                                current = se;
                                found = true;
                                break;
                            }
                        }
                    }
                    break;
                }
            }
            if (!found) return false;
        }

        c--;
    }

    return true;
}

}
