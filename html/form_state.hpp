#pragma once
#include "../tests/utility.hpp"
#include "dom.hpp"

#include <string>
#include <unordered_map>

namespace browser::html {

    struct FormState {
        std::unordered_map<const Element *, std::string> input_values;
        std::unordered_map<const Element *, bool> check_states;
        std::unordered_map<const Element *, int> select_indices;
        Element *focused_element = nullptr;
        Element *hovered_element = nullptr;
        u32 caret_position = 0;
        bool caret_visible = true;

        void set_value(const Element *el, const std::string &val);
        std::string get_value(const Element *el) const;
        void set_checked(const Element *el, bool checked);
        bool is_checked(const Element *el) const;
        void set_selected_index(const Element *el, int idx);
        int get_selected_index(const Element *el) const;
        void focus(Element *el);
        void blur();
        void toggle_checkbox(Element *el);
    };

    extern FormState g_form_state;

}  // namespace browser::html
