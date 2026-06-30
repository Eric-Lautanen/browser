#include "selector_match.hpp"

#include <cctype>
#include <cmath>

namespace browser::css {

    namespace {

        bool iequal(const std::string &a, const std::string &b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        bool match_simple(const SimpleSelector &ss, const html::Element *el, const html::Node *doc = nullptr) {
            switch (ss.type) {
                case SimpleSelector::Type::TAG:
                    return iequal(ss.name, el->tag_name);
                case SimpleSelector::Type::UNIVERSAL:
                    return true;
                case SimpleSelector::Type::CLASS: {
                    auto classes = el->class_list();
                    for (const auto &cls : classes) {
                        if (cls == ss.name)
                            return true;
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
                        const std::string &remaining = val;
                        size_t pos = 0;
                        while (pos < remaining.size()) {
                            while (pos < remaining.size() && (remaining[pos] == ' ' || remaining[pos] == '\t' ||
                                   remaining[pos] == '\n' || remaining[pos] == '\f' || remaining[pos] == '\r'))
                                pos++;
                            if (pos >= remaining.size())
                                break;
                            size_t end = pos;
                            while (end < remaining.size() && remaining[end] != ' ' && remaining[end] != '\t' &&
                                   remaining[end] != '\n' && remaining[end] != '\f' && remaining[end] != '\r')
                                end++;
                            if (remaining.substr(pos, end - pos) == ss.value)
                                return true;
                            pos = end;
                        }
                        return false;
                        return false;
                    }
                    if (ss.match_operator == '|') {
                        return val == ss.value ||
                               (val.size() > ss.value.size() && val.substr(0, ss.value.size()) == ss.value &&
                                val[ss.value.size()] == '-');
                    }
                    if (ss.match_operator == '^') {
                        return val.size() >= ss.value.size() && val.compare(0, ss.value.size(), ss.value) == 0;
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
                        if (!el->parent || el->parent->children.empty())
                            return false;
                        return el->parent->children.front().get() == el;
                    }
                    if (ss.name == "last-child") {
                        if (!el->parent || el->parent->children.empty())
                            return false;
                        return el->parent->children.back().get() == el;
                    }
                    if (ss.name == "only-child") {
                        if (!el->parent)
                            return false;
                        u32 count = 0;
                        for (auto &c : el->parent->children) {
                            if (c->type == html::NodeType::ELEMENT)
                                count++;
                        }
                        return count == 1;
                    }
                    if (ss.name == "root") {
                        return el->parent && el->parent->type == html::NodeType::DOCUMENT;
                    }
                    if (ss.name == "empty") {
                        if (!el->children.empty()) {
                            // Check if any child is an element or non-empty text
                            for (auto &c : el->children) {
                                if (c->type == html::NodeType::ELEMENT)
                                    return false;
                                if (c->type == html::NodeType::TEXT) {
                                    auto *t = static_cast<html::Text *>(c.get());
                                    for (char ch : t->data) {
                                        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
                                            return false;
                                    }
                                }
                            }
                        }
                        return true;
                    }
                    if (ss.name == "disabled") {
                        return el->has_attribute("disabled");
                    }
                    if (ss.name == "enabled") {
                        auto tag = el->tag_name;
                        for (auto &c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        return (tag == "input" || tag == "button" || tag == "select" || tag == "textarea") &&
                               !el->has_attribute("disabled");
                    }
                    if (ss.name == "checked") {
                        return el->has_attribute("checked");
                    }
                    if (ss.name == "target") {
                        (void)doc;
                        return false;  // URL fragment matching is handled externally; never wildcard-match all IDs
                    }
                    if (ss.name == "nth-child") {
                        if (!el->parent)
                            return false;
                        i32 idx = 0;
                        for (auto &sib : el->parent->children) {
                            if (sib.get() == el)
                                break;
                            if (sib->type == html::NodeType::ELEMENT)
                                idx++;
                        }
                        idx++;
                        if (ss.nth_args.is_odd)
                            return idx % 2 == 1;
                        if (ss.nth_args.is_even)
                            return idx % 2 == 0;
                        if (ss.nth_args.a == 0)
                            return idx == ss.nth_args.b;
                        if ((idx - ss.nth_args.b) % ss.nth_args.a != 0)
                            return false;
                        if (ss.nth_args.a > 0)
                            return idx >= ss.nth_args.b;
                        return idx <= ss.nth_args.b;
                    }
                    if (ss.name == "nth-last-child") {
                        if (!el->parent)
                            return false;
                        i32 total = 0;
                        for (auto &sib : el->parent->children) {
                            if (sib->type == html::NodeType::ELEMENT)
                                total++;
                        }
                        i32 idx = 0;
                        for (auto &sib : el->parent->children) {
                            if (sib.get() == el)
                                break;
                            if (sib->type == html::NodeType::ELEMENT)
                                idx++;
                        }
                        idx = total - idx;
                        if (ss.nth_args.is_odd)
                            return idx % 2 == 1;
                        if (ss.nth_args.a == 0)
                            return idx == ss.nth_args.b;
                        if ((idx - ss.nth_args.b) % ss.nth_args.a != 0)
                            return false;
                        if (ss.nth_args.a > 0)
                            return idx >= ss.nth_args.b;
                        return idx <= ss.nth_args.b;
                            return false;
                        return (idx - ss.nth_args.b) % ss.nth_args.a == 0;
                    }
                    if (ss.name == "first-of-type") {
                        if (!el->parent)
                            return false;
                        for (auto &sib : el->parent->children) {
                            if (sib.get() == el)
                                return true;
                            if (sib->type == html::NodeType::ELEMENT) {
                                auto *se = static_cast<html::Element *>(sib.get());
                                if (se->tag_name == el->tag_name)
                                    return false;
                            }
                        }
                        return true;
                    }
                    if (ss.name == "last-of-type") {
                        if (!el->parent)
                            return false;
                        for (i32 i = static_cast<i32>(el->parent->children.size()) - 1; i >= 0; i--) {
                            auto &sib = el->parent->children[static_cast<u32>(i)];
                            if (sib.get() == el)
                                return true;
                            if (sib->type == html::NodeType::ELEMENT) {
                                auto *se = static_cast<html::Element *>(sib.get());
                                if (se->tag_name == el->tag_name)
                                    return false;
                            }
                        }
                        return true;
                    }
                    if (ss.name == "not") {
                        if (ss.argument_selectors.empty())
                            return true;
                        for (const auto &arg_sel : ss.argument_selectors) {
                            if (matches_selector(arg_sel, el, doc))
                                return false;
                        }
                        return true;
                    }
                    if (ss.name == "is" || ss.name == "where") {
                        if (ss.argument_selectors.empty())
                            return true;
                        for (const auto &arg_sel : ss.argument_selectors) {
                            if (matches_selector(arg_sel, el, doc))
                                return true;
                        }
                        return false;
                    }
                    // Dynamic pseudo-classes - evaluated later via element state
                    if (ss.name == "hover" || ss.name == "focus" || ss.name == "active" || ss.name == "visited" ||
                        ss.name == "link" || ss.name == "focus-within" || ss.name == "focus-visible") {
                        // These are evaluated based on element state stored externally.
                        // In static cascade, only :link matches <a> elements with href
                        if (ss.name == "link") {
                            std::string href = el->get_attribute("href");
                            return !href.empty();
                        }
                        // :visited also matches <a[href]> but with different styling
                        if (ss.name == "visited") {
                            return false;  // Not visited in static analysis
                        }
                        // :hover/:focus/:active should NOT match in static cascade
                        // (they only match when triggered interactively)
                        std::string state = el->get_attribute("data-pseudo-state");
                        if (!state.empty()) {
                            return state.find(ss.name) != std::string::npos;
                        }
                        return false;
                    }
                    return false;
                }
                case SimpleSelector::Type::PSEUDO_ELEMENT: {
                    // Pseudo-elements select into generated content
                    if (ss.name == "before" || ss.name == "after" || ss.name == "first-line" ||
                        ss.name == "first-letter") {
                        return true;  // These are handled during cascade
                    }
                    return false;
                }
            }
            return false;
        }

        const html::Node *find_previous_sibling(const html::Node *node) {
            if (!node || !node->parent)
                return nullptr;
            for (size_t i = 0; i < node->parent->children.size(); i++) {
                if (node->parent->children[i].get() == node) {
                    if (i == 0)
                        return nullptr;
                    return node->parent->children[i - 1].get();
                }
            }
            return nullptr;
        }

    }  // anonymous namespace

    bool matches_compound(const std::vector<SimpleSelector> &compound,
                          const html::Element *el,
                          const html::Node *doc = nullptr) {
        if (!el)
            return false;
        for (const auto &ss : compound) {
            if (!match_simple(ss, el, doc))
                return false;
        }
        return true;
    }

    bool matches_selector(const Selector &sel, const html::Element *el, const html::Node *root) {
        if (sel.compounds.empty() || !el)
            return false;

        const html::Element *current = el;
        int c = static_cast<int>(sel.compounds.size()) - 1;

        if (!matches_compound(sel.compounds[c].simples, current, root))
            return false;
        c--;

        while (c >= 0 && current) {
            auto comb = sel.combinators[c];

            if (comb == Combinator::CHILD) {
                if (!current->parent || current->parent->type != html::NodeType::ELEMENT)
                    return false;
                auto *parent = static_cast<const html::Element *>(current->parent);
                if (!matches_compound(sel.compounds[c].simples, parent, root))
                    return false;
                current = parent;
            } else if (comb == Combinator::DESCENDANT) {
                bool found = false;
                const html::Node *ancestor = current->parent;
                while (ancestor && ancestor != root) {
                    if (ancestor->type == html::NodeType::ELEMENT) {
                        auto *ae = static_cast<const html::Element *>(ancestor);
                        if (matches_compound(sel.compounds[c].simples, ae, root)) {
                            current = ae;
                            found = true;
                            break;
                        }
                    }
                    ancestor = ancestor->parent;
                }
                if (!found)
                    return false;
            } else if (comb == Combinator::ADJACENT_SIBLING) {
                auto *prev = find_previous_sibling(current);
                if (!prev || prev->type != html::NodeType::ELEMENT)
                    return false;
                auto *pe = static_cast<const html::Element *>(prev);
                if (!matches_compound(sel.compounds[c].simples, pe, root))
                    return false;
                current = pe;
            } else if (comb == Combinator::GENERAL_SIBLING) {
                bool found = false;
                if (!current->parent)
                    return false;
                for (size_t i = 0; i < current->parent->children.size(); i++) {
                    if (current->parent->children[i].get() == current) {
                        for (int j = static_cast<int>(i) - 1; j >= 0; j--) {
                            auto *sibling = current->parent->children[j].get();
                            if (sibling->type == html::NodeType::ELEMENT) {
                                auto *se = static_cast<const html::Element *>(sibling);
                                if (matches_compound(sel.compounds[c].simples, se, root)) {
                                    current = se;
                                    found = true;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
                if (!found)
                    return false;
            }

            c--;
        }

        return true;
    }

}  // namespace browser::css
