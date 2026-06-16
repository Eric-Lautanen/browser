#include "selector_match.hpp"
#include <cctype>
#include <cmath>

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
            if (ss.name == "empty") {
                return el->children.empty();
            }
            if (ss.name == "disabled") {
                return el->has_attribute("disabled");
            }
            if (ss.name == "enabled") {
                auto tag = el->tag_name;
                for (auto& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return (tag == "input" || tag == "button" || tag == "select" || tag == "textarea") &&
                       !el->has_attribute("disabled");
            }
            if (ss.name == "checked") {
                return el->has_attribute("checked");
            }
            if (ss.name == "target") {
                return el->has_attribute("id"); // simplified: matches if element has an id
            }
            if (ss.name.substr(0, 10) == "nth-child(") {
                std::string inner = ss.name.substr(10);
                if (!inner.empty() && inner.back() == ')') inner.pop_back();
                // Find element index among siblings (1-based)
                if (!el->parent) return false;
                i32 idx = 0;
                for (auto& sib : el->parent->children) {
                    if (sib.get() == el) break;
                    if (sib->type == html::NodeType::ELEMENT) idx++;
                }
                idx++; // 1-based

                if (inner == "odd") return idx % 2 == 1;
                if (inner == "even") return idx % 2 == 0;

                // Parse an+b
                i32 a = 0, b = 0;
                int n_pos = inner.find('n');
                if (n_pos != -1) {
                    std::string a_part = inner.substr(0, n_pos);
                    if (a_part.empty() || a_part == "+") a = 1;
                    else if (a_part == "-") a = -1;
                    else a = static_cast<i32>(std::strtol(a_part.c_str(), nullptr, 10));
                    std::string b_part = inner.substr(n_pos + 1);
                    if (!b_part.empty()) {
                        if (b_part[0] == '+') b_part = b_part.substr(1);
                        b = static_cast<i32>(std::strtol(b_part.c_str(), nullptr, 10));
                    }
                } else {
                    b = static_cast<i32>(std::strtol(inner.c_str(), nullptr, 10));
                }
                if (a == 0) return idx == b;
                if (idx < b) return false;
                return (idx - b) % a == 0;
            }
            if (ss.name.substr(0, 5) == "not(") {
                // Negation pseudo-class - simplified: parse inner simple selector
                std::string inner = ss.name.substr(4);
                if (!inner.empty() && inner.back() == ')') inner.pop_back();
                // Trim whitespace
                while (!inner.empty() && (inner.back() == ' ' || inner.back() == '\t')) inner.pop_back();
                while (!inner.empty() && (inner[0] == ' ' || inner[0] == '\t')) inner = inner.substr(1);
                if (inner.empty()) return true;
                // Check if this element matches the negated selector
                if (inner[0] == '.') {
                    // Class negation
                    return !match_simple({SimpleSelector::Type::CLASS, inner.substr(1), "", 0}, el);
                }
                if (inner[0] == '#') {
                    return !match_simple({SimpleSelector::Type::ID, inner.substr(1), "", 0}, el);
                }
                // Tag negation
                SimpleSelector tag_sel;
                tag_sel.type = SimpleSelector::Type::TAG;
                tag_sel.name = inner;
                return !match_simple(tag_sel, el);
            }
            // Dynamic pseudo-classes - evaluated later via element state
            if (ss.name == "hover" || ss.name == "focus" || ss.name == "active" ||
                ss.name == "visited" || ss.name == "link" || ss.name == "focus-within" ||
                ss.name == "focus-visible") {
                // These are evaluated based on element state stored externally.
                // For now, match by default unless we can check state
                std::string state = el->get_attribute("data-pseudo-state");
                if (!state.empty()) {
                    return state.find(ss.name) != std::string::npos;
                }
                return true; // Match by default for correct static rendering
            }
            return false;
        }
        case SimpleSelector::Type::PSEUDO_ELEMENT: {
            // Pseudo-elements select into generated content
            if (ss.name == "before" || ss.name == "after" ||
                ss.name == "first-line" || ss.name == "first-letter") {
                return true; // These are handled during cascade
            }
            return false;
        }
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
