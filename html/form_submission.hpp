#pragma once
#include "../net/http_client.hpp"
#include "dom.hpp"

#include <string>

namespace browser::html {

    std::string submit_form(const Element *form_element, net::HTTPClient *http = nullptr);
    std::string handle_form_submission(const Element *submit_button, net::HTTPClient *http = nullptr);

    // Returns empty string if validation passes, or error message if it fails
    std::string validate_field(const Element *el);

    std::string url_encode(const std::string &s);
    std::string url_decode(const std::string &s);

}  // namespace browser::html
