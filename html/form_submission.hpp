#pragma once
#include "dom.hpp"

#include <string>

namespace browser::html {

    std::string submit_form(const Element *form_element);
    std::string handle_form_submission(const Element *submit_button);

    std::string url_encode(const std::string &s);
    std::string url_decode(const std::string &s);

}  // namespace browser::html
