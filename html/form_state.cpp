#include "form_state.hpp"

namespace browser::html {

    FormState g_form_state;

    void FormState::set_value(const Element *el, const std::string &val) {
        input_values[el] = val;
    }

    std::string FormState::get_value(const Element *el) const {
        auto it = input_values.find(el);
        if (it != input_values.end())
            return it->second;
        return el ? el->get_attribute("value") : "";
    }

    void FormState::set_checked(const Element *el, bool checked) {
        check_states[el] = checked;
    }

    bool FormState::is_checked(const Element *el) const {
        auto it = check_states.find(el);
        if (it != check_states.end())
            return it->second;
        return el ? el->has_attribute("checked") : false;
    }

    void FormState::set_selected_index(const Element *el, int idx) {
        select_indices[el] = idx;
    }

    int FormState::get_selected_index(const Element *el) const {
        auto it = select_indices.find(el);
        if (it != select_indices.end())
            return it->second;
        return 0;
    }

    void FormState::focus(Element *el) {
        if (focused_element == el)
            return;
        blur();
        focused_element = el;
        caret_position = 0;
        caret_visible = true;
    }

    void FormState::blur() {
        if (focused_element) {
            focused_element = nullptr;
        }
        caret_position = 0;
    }

    void FormState::toggle_checkbox(Element *el) {
        bool current = is_checked(el);
        set_checked(el, !current);
    }

}  // namespace browser::html
