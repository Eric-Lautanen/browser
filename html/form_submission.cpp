#include "form_submission.hpp"

#include "../net/http.hpp"
#include "../net/http_client.hpp"
#include "../net/url.hpp"
#include "dom.hpp"
#include "form_state.hpp"
#include "traversal.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace browser::html {

    namespace {

        std::string encode_for_form(const std::string &s) {
            std::string result;
            for (unsigned char c : s) {
                if (c == ' ') {
                    result += '+';
                } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_' ||
                           c == '~') {
                    result += static_cast<char>(c);
                } else {
                    const char hex[] = "0123456789ABCDEF";
                    result += '%';
                    result += hex[c >> 4];
                    result += hex[c & 0xF];
                }
            }
            return result;
        }

        const Element *find_parent_form(const Element *el) {
            Node *p = el->parent;
            while (p) {
                if (p->type == NodeType::ELEMENT) {
                    auto *pe = static_cast<Element *>(p);
                    if (pe->tag_name == "form")
                        return pe;
                }
                p = p->parent;
            }
            return nullptr;
        }

        void collect_form_data(const Element *form, std::vector<std::pair<std::string, std::string>> &data) {
            traverse_depth_first(const_cast<Element *>(form), [&](Node *n) {
                if (n->type != NodeType::ELEMENT)
                    return;
                auto *el = static_cast<Element *>(n);
                if (el == form)
                    return;

                std::string tag = el->tag_name;
                std::string name = el->get_attribute("name");
                if (name.empty())
                    return;

                if (tag == "input") {
                    std::string type = el->get_attribute("type");
                    if (type == "checkbox") {
                        if (g_form_state.is_checked(el)) {
                            std::string val = g_form_state.get_value(el);
                            if (val.empty())
                                val = "on";
                            data.push_back({name, val});
                        }
                    } else if (type == "radio") {
                        if (g_form_state.is_checked(el)) {
                            std::string val = g_form_state.get_value(el);
                            data.push_back({name, val});
                        }
                    } else if (type == "submit") {
                        // Skip submit buttons unless clicked
                    } else {
                        std::string val = g_form_state.get_value(el);
                        data.push_back({name, val});
                    }
                } else if (tag == "textarea") {
                    std::string val = g_form_state.get_value(el);
                    data.push_back({name, val});
                } else if (tag == "select") {
                    int idx = g_form_state.get_selected_index(el);
                    // Find the nth option child
                    int count = 0;
                    std::string selected_val;
                    for (auto &child : el->children) {
                        if (child->type == NodeType::ELEMENT) {
                            auto *opt = static_cast<Element *>(child.get());
                            if (opt->tag_name == "option") {
                                if (count == idx) {
                                    selected_val = opt->get_attribute("value");
                                    if (selected_val.empty())
                                        selected_val = inner_text(opt);
                                    break;
                                }
                                count++;
                            }
                        }
                    }
                    data.push_back({name, selected_val});
                }
            });
        }

    }  // namespace

    std::string url_encode(const std::string &s) {
        return encode_for_form(s);
    }

    std::string url_decode(const std::string &s) {
        return net::url_decode(s);
    }

    std::string submit_form(const Element *form_element, net::HTTPClient *http) {
        if (!form_element || form_element->tag_name != "form")
            return {};

        std::string action = form_element->get_attribute("action");
        std::string method = form_element->get_attribute("method");
        for (auto &c : method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        std::string base_url;
        {
            Node *p = const_cast<Element *>(form_element);
            while (p->parent) p = p->parent;
            if (p->type == NodeType::DOCUMENT) {
                base_url = static_cast<Document *>(p)->url;
            }
        }

        if (action.empty())
            action = base_url;

        // Resolve relative action URL against base_url
        if (!base_url.empty() && action.find("://") == std::string::npos) {
            auto base_parsed = net::URL::parse(base_url);
            if (base_parsed.is_ok()) {
                auto resolved = base_parsed.unwrap().resolve(action);
                if (resolved.is_ok())
                    action = resolved.unwrap().to_string();
            }
        }

        // Collect form data
        std::vector<std::pair<std::string, std::string>> form_data;
        collect_form_data(form_element, form_data);

        // Encode as application/x-www-form-urlencoded
        std::string encoded_body;
        for (size_t i = 0; i < form_data.size(); i++) {
            if (i > 0)
                encoded_body += '&';
            encoded_body += url_encode(form_data[i].first) + "=" + url_encode(form_data[i].second);
        }

        if (method == "POST") {
            net::http::Request req;
            auto url_r = net::URL::parse(action);
            if (url_r.is_err())
                return {};
            req.method = net::http::Method::POST;
            req.url = url_r.unwrap();
            {
                std::string host_hdr = req.url.host;
                if (req.url.port != 0 && req.url.port != req.url.default_port())
                    host_hdr += ":" + std::to_string(req.url.port);
                req.headers.set("Host", host_hdr);
            }
            req.headers.set("Content-Type", "application/x-www-form-urlencoded");
            req.headers.set("Content-Length", std::to_string(encoded_body.size()));
            req.headers.set("Connection", "close");
            req.body.assign(encoded_body.begin(), encoded_body.end());

            if (http) {
                http->fetch_async(req).sync_wait();
            }
            return action;
        }

        // GET method: append encoded data to URL
        if (encoded_body.empty())
            return action;
        if (action.find('?') != std::string::npos)
            action += '&';
        else
            action += '?';
        action += encoded_body;
        return action;
    }

    std::string handle_form_submission(const Element *submit_button, net::HTTPClient *http) {
        if (!submit_button)
            return {};

        const Element *form = find_parent_form(submit_button);
        if (!form) {
            if (submit_button->tag_name == "form")
                form = submit_button;
        }
        if (!form)
            return {};

        return submit_form(form, http);
    }

}  // namespace browser::html
