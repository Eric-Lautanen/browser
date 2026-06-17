#include "../parser.hpp"
#include "../utf8.hpp"
#include <unordered_set>

namespace browser::html {

Parser::Parser() : preload_scanner_(nullptr) {}

Parser::Parser(PreloadScanner* scanner, const std::string& base_url)
    : preload_scanner_(scanner), base_url_(base_url) {}

std::unique_ptr<Document> Parser::parse(const std::string& html) {
    document_ = create_document();
    tokenizer_ = std::make_unique<Tokenizer>();
    tokenizer_->feed(html);
    tokenizer_->end();

    while (tokenizer_->has_next()) {
        Token t = tokenizer_->next();
        if (preload_scanner_) {
            preload_scanner_->scan_token(t, base_url_);
        }
        handle_token(t);
    }
    flush_pending_text();

    return std::move(document_);
}

Element* Parser::current_node() const {
    return stack_.empty() ? nullptr : stack_.back();
}

void Parser::handle_token(const Token& token) {
    switch (mode_) {
        case InsertionMode::INITIAL: handle_initial(token); break;
        case InsertionMode::BEFORE_HTML: handle_before_html(token); break;
        case InsertionMode::BEFORE_HEAD: handle_before_head(token); break;
        case InsertionMode::IN_HEAD: handle_in_head(token); break;
        case InsertionMode::AFTER_HEAD: handle_after_head(token); break;
        case InsertionMode::IN_BODY: handle_in_body(token); break;
        case InsertionMode::TEXT: handle_text(token); break;
        case InsertionMode::IN_TABLE: handle_in_table(token); break;
        case InsertionMode::IN_TABLE_BODY: handle_in_table_body(token); break;
        case InsertionMode::IN_ROW: handle_in_row(token); break;
        case InsertionMode::IN_CELL: handle_in_cell(token); break;
        case InsertionMode::IN_SELECT: handle_in_select(token); break;
        case InsertionMode::IN_SELECT_IN_TABLE: handle_in_select(token); break;
        case InsertionMode::IN_CAPTION: handle_in_caption(token); break;
        case InsertionMode::IN_COLUMN_GROUP: handle_in_column_group(token); break;
        case InsertionMode::IN_TEMPLATE: handle_in_template(token); break;
        case InsertionMode::IN_FRAMESET: handle_in_frameset(token); break;
        case InsertionMode::AFTER_FRAMESET: handle_after_frameset(token); break;
        case InsertionMode::AFTER_AFTER_FRAMESET: handle_after_after_frameset(token); break;
        case InsertionMode::AFTER_BODY: handle_after_body(token); break;
        case InsertionMode::AFTER_AFTER_BODY: handle_after_after_body(token); break;
        default: break;
    }
}

void Parser::reset_insertion_mode() {
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        auto* node = stack_[static_cast<u32>(i)];
        if (!node) continue;
        const std::string& t = node->tag_name;
        if (t == "select") {
            mode_ = InsertionMode::IN_SELECT;
            return;
        }
        if (t == "td" || t == "th") {
            mode_ = InsertionMode::IN_CELL;
            return;
        }
        if (t == "tr") {
            mode_ = InsertionMode::IN_ROW;
            return;
        }
        if (t == "tbody" || t == "thead" || t == "tfoot") {
            mode_ = InsertionMode::IN_TABLE_BODY;
            return;
        }
        if (t == "caption") {
            mode_ = InsertionMode::IN_CAPTION;
            return;
        }
        if (t == "colgroup") {
            mode_ = InsertionMode::IN_COLUMN_GROUP;
            return;
        }
        if (t == "table") {
            mode_ = InsertionMode::IN_TABLE;
            return;
        }
        if (t == "template") {
            if (!template_modes_.empty()) {
                mode_ = template_modes_.back();
            } else {
                mode_ = InsertionMode::IN_BODY;
            }
            return;
        }
        if (t == "head") {
            if (t == "head") {
                mode_ = InsertionMode::IN_HEAD;
                return;
            }
        }
        if (t == "body") {
            mode_ = InsertionMode::IN_BODY;
            return;
        }
        if (t == "frameset") {
            mode_ = InsertionMode::IN_FRAMESET;
            return;
        }
        if (t == "noframes") {
            mode_ = InsertionMode::IN_BODY;
            return;
        }
        if (t == "html") {
            if (head_element_pointer_ == nullptr) {
                mode_ = InsertionMode::BEFORE_HEAD;
            } else {
                mode_ = InsertionMode::AFTER_HEAD;
            }
            return;
        }
    }
    mode_ = InsertionMode::IN_BODY;
}

void Parser::insert_character(char32_t c) {
    flush_pending_text();
    pending_text_ += encode_utf8(c);
}

void Parser::insert_comment(const std::string& data) {
    auto c = std::make_unique<Comment>();
    c->data = data;
    if (current_node()) {
        append_child(current_node(), std::move(c));
    }
}

void Parser::insert_doctype(const DoctypeToken& token) {
    auto dt = std::make_unique<DocumentType>();
    dt->name = token.name;
    dt->public_id = token.public_id;
    dt->system_id = token.system_id;
    dt->force_quirks = token.force_quirks;
    append_child(document_.get(), std::move(dt));
}

Element* Parser::create_element_for_token(const TagToken& token) {
    auto el = std::make_unique<Element>(token.tag_name);
    for (const auto& attr : token.attributes) {
        el->attributes[attr.name] = attr.value;
    }
    if (token.tag_name == "svg" || token.tag_name == "math") {
        if (token.tag_name == "svg") {
            el->namespace_uri = "http://www.w3.org/2000/svg";
        } else {
            el->namespace_uri = "http://www.w3.org/1998/Math/MathML";
        }
        foreign_ = true;
    } else if (foreign_) {
        if (token.tag_name == "foreignobject") {
            // <foreignObject> inside SVG switches back to HTML
            el->namespace_uri = "http://www.w3.org/1999/xhtml";
        } else if (token.tag_name == "annotation-xml") {
            // <annotation-xml> in MathML can contain HTML
            el->namespace_uri = "http://www.w3.org/1999/xhtml";
        } else if (token.tag_name == "style" || token.tag_name == "script" || token.tag_name == "title") {
            // These are HTML-encoded even inside SVG/MathML
            el->namespace_uri = "http://www.w3.org/1999/xhtml";
        } else {
            // Default: use the parent's namespace (SVG or MathML)
            if (!stack_.empty() && stack_.back()) {
                el->namespace_uri = stack_.back()->namespace_uri;
            }
        }
    }
    return el.release();
}

void Parser::generate_implied_end_tags(const std::vector<std::string>& exceptions) {
    static const std::unordered_set<std::string> implied_tags = {
        "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"
    };
    while (current_node()) {
        std::string tag = current_node()->tag_name;
        if (implied_tags.find(tag) != implied_tags.end()) {
            bool is_exception = false;
            for (const auto& e : exceptions) {
                if (tag == e) { is_exception = true; break; }
            }
            if (is_exception) break;
            stack_.pop_back();
        } else {
            break;
        }
    }
}

bool Parser::has_element_in_scope(const std::string& tag_name, const std::vector<std::string>& extras) {
    static const std::unordered_set<std::string> scope_tags = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object",
        "template", "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        "foreignobject", "desc", "title"
    };
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (scope_tags.find(stack_[i]->tag_name) != scope_tags.end()) return false;
        for (const auto& e : extras) {
            if (stack_[i]->tag_name == e) return false;
        }
    }
    return false;
}

bool Parser::has_element_in_scope(const std::vector<std::string>& tags) {
    for (const auto& tag : tags) {
        if (has_element_in_scope(tag)) return true;
    }
    return false;
}

bool Parser::has_element_in_list_scope(const std::string& tag_name) {
    static const std::unordered_set<std::string> list_scope = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object",
        "template", "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        "foreignobject", "desc", "title", "ol", "ul"
    };
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (list_scope.find(stack_[i]->tag_name) != list_scope.end()) return false;
    }
    return false;
}

bool Parser::has_element_in_button_scope(const std::string& tag_name) {
    static const std::unordered_set<std::string> button_scope = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object",
        "template", "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        "foreignobject", "desc", "title", "button"
    };
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (button_scope.find(stack_[i]->tag_name) != button_scope.end()) return false;
    }
    return false;
}

bool Parser::has_element_in_table_scope(const std::string& tag_name) {
    static const std::unordered_set<std::string> table_scope = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object",
        "template", "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        "foreignobject", "desc", "title"
    };
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (table_scope.find(stack_[i]->tag_name) != table_scope.end()) return false;
    }
    return false;
}

bool Parser::has_element_in_select_scope(const std::string& tag_name) {
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (stack_[i]->tag_name == "select") return false;
    }
    return false;
}

void Parser::close_element(const std::string& tag_name) {
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (stack_[i] && stack_[i]->tag_name == tag_name) {
            stack_.resize(static_cast<u32>(i));
            return;
        }
    }
}

void Parser::clear_stack_back_to_table_context() {
    while (current_node() && current_node()->tag_name != "table" &&
           current_node()->tag_name != "template" && current_node()->tag_name != "html") {
        stack_.pop_back();
    }
}

void Parser::clear_stack_back_to_table_body_context() {
    while (current_node() && current_node()->tag_name != "tbody" &&
           current_node()->tag_name != "tfoot" && current_node()->tag_name != "thead" &&
           current_node()->tag_name != "template" && current_node()->tag_name != "html") {
        stack_.pop_back();
    }
}

void Parser::clear_stack_back_to_table_row_context() {
    while (current_node() && current_node()->tag_name != "tr" &&
           current_node()->tag_name != "template" && current_node()->tag_name != "html") {
        stack_.pop_back();
    }
}

bool Parser::is_special_tag(const std::string& tag) {
    static const std::unordered_set<std::string> special = {
        "address", "applet", "area", "article", "aside", "base", "basefont", "bgsound",
        "blockquote", "body", "br", "button", "caption", "center", "col", "colgroup",
        "dd", "details", "dir", "div", "dl", "dt", "embed", "fieldset", "figcaption",
        "figure", "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6",
        "head", "header", "hgroup", "hr", "html", "iframe", "img", "input", "isindex",
        "li", "link", "listing", "main", "marquee", "menu", "meta", "nav", "noembed",
        "noframes", "noscript", "object", "ol", "p", "param", "plaintext", "pre",
        "script", "section", "select", "source", "style", "summary", "table",
        "tbody", "td", "template", "textarea", "tfoot", "th", "thead", "title",
        "tr", "track", "ul", "wbr", "xmp"
    };
    return special.find(tag) != special.end();
}

bool is_void_element(const std::string& tag) {
    static const std::unordered_set<std::string> voids = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    return voids.find(tag) != voids.end();
}

bool Parser::is_heading_tag(const std::string& tag) {
    return tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
}

void Parser::parse_generic_start_tag(const TagToken& tag) {
    const std::string& t = tag.tag_name;

    if (t == "html") {
        if (!stack_.empty() && stack_[0]->tag_name == "html") {
            for (const auto& attr : tag.attributes) {
                if (stack_[0]->attributes.find(attr.name) == stack_[0]->attributes.end()) {
                    stack_[0]->attributes[attr.name] = attr.value;
                }
            }
            return;
        }
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "base" || t == "basefont" || t == "bgsound" || t == "link" || t == "meta") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        return;
    }

    if (t == "script") {
        flush_pending_text();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag("script");
        tokenizer_->set_state(4); // SCRIPT_DATA
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        return;
    }
    if (t == "style" || t == "xmp" || t == "iframe" || t == "noembed" || t == "noframes") {
        flush_pending_text();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag(t);
        tokenizer_->set_state(2); // RAWTEXT
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        return;
    }
    if (t == "title" || t == "textarea") {
        flush_pending_text();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag(t);
        tokenizer_->set_state(1); // RCDATA
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        return;
    }
    if (t == "noscript") {
        flush_pending_text();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag("noscript");
        tokenizer_->set_state(2); // RAWTEXT
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        return;
    }

    if (t == "body") {
        if (stack_.size() < 2 || stack_[1]->tag_name != "body") return;
        frameset_ok_ = false;
        for (const auto& attr : tag.attributes) {
            if (stack_[1]->attributes.find(attr.name) == stack_[1]->attributes.end()) {
                stack_[1]->attributes[attr.name] = attr.value;
            }
        }
        return;
    }

    if (t == "frameset") {
        if (!frameset_ok_) return;
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        mode_ = InsertionMode::AFTER_HEAD_FRAMESET;
        return;
    }

    if (t == "address" || t == "article" || t == "aside" || t == "blockquote" || t == "center" ||
        t == "details" || t == "dialog" || t == "dir" || t == "div" || t == "dl" || t == "fieldset" ||
        t == "figcaption" || t == "figure" || t == "footer" || t == "header" || t == "hgroup" ||
        t == "main" || t == "nav" || t == "ol" || t == "p" || t == "section" || t == "summary" ||
        t == "ul") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6") {
        if (has_element_in_scope("p")) close_element("p");
        if (current_node() && is_heading_tag(current_node()->tag_name)) {
            stack_.pop_back();
        }
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "textarea") {
        flush_pending_text();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag("textarea");
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        frameset_ok_ = false;
        return;
    }

    if (t == "pre" || t == "listing") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        frameset_ok_ = false;
        return;
    }

    if (t == "form") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        frameset_ok_ = false;
        return;
    }

    if (t == "option") {
        if (has_element_in_list_scope("option")) {
            generate_implied_end_tags({"option"});
            close_element("option");
        }
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "optgroup") {
        if (has_element_in_list_scope("optgroup")) {
            generate_implied_end_tags({"optgroup"});
            close_element("optgroup");
        }
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "select") {
        reconstruct_active_formatting_elements();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        mode_ = InsertionMode::IN_SELECT;
        frameset_ok_ = false;
        return;
    }

    if (t == "li") {
        frameset_ok_ = false;
        if (has_element_in_list_scope("li")) {
            generate_implied_end_tags({"li"});
            close_element("li");
        }
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "dd" || t == "dt") {
        frameset_ok_ = false;
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[i] && (stack_[i]->tag_name == "dd" || stack_[i]->tag_name == "dt")) {
                stack_.resize(static_cast<u32>(i));
                break;
            }
        }
        generate_implied_end_tags({"dd", "dt"});
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "template") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        template_modes_.push_back(InsertionMode::IN_TEMPLATE);
        frameset_ok_ = false;
        return;
    }

    if (t == "plaintext") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "a") {
        for (i32 i = static_cast<i32>(active_formatting_elements_.size()) - 1; i >= 0; i--) {
            if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) break;
            if (active_formatting_elements_[static_cast<u32>(i)] &&
                active_formatting_elements_[static_cast<u32>(i)]->tag_name == "a") {
                adoption_agency_algorithm("a");
                break;
            }
        }
        reconstruct_active_formatting_elements();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        push_active_formatting_element(el);
        return;
    }

    if (t == "b" || t == "big" || t == "code" || t == "em" || t == "font" || t == "i" ||
        t == "s" || t == "small" || t == "strike" || t == "strong" || t == "tt" || t == "u") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        push_active_formatting_element(el);
        return;
    }

    if (t == "nobr") {
        if (has_element_in_scope("nobr")) {
            adoption_agency_algorithm("nobr");
        }
        reconstruct_active_formatting_elements();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        push_active_formatting_element(el);
        return;
    }

    if (t == "br") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        stack_.pop_back();
        frameset_ok_ = false;
        return;
    }

    if (t == "p") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "span") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (is_void_element(t)) {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        if (t == "input" || t == "hr" || t == "img" || t == "embed" || t == "wbr") {
        }
        if (t == "br") frameset_ok_ = false;
        return;
    }

    if (t == "table") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        frameset_ok_ = false;
        mode_ = InsertionMode::IN_TABLE;
        return;
    }

    if (t == "image") {
        TagToken img = tag;
        img.tag_name = "img";
        parse_generic_start_tag(img);
        return;
    }

    if (t == "hr") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        frameset_ok_ = false;
        return;
    }

    if (t == "ol" || t == "ul") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    auto* el = create_element_for_token(tag);
    insert_element(el);
    if (!is_void_element(t)) {
        stack_.push_back(el);
    }
}

void Parser::parse_generic_end_tag(const TagToken& tag) {
    const std::string& t = tag.tag_name;

    if (t == "body") {
        if (!has_element_in_scope("body")) return;
        mode_ = InsertionMode::AFTER_BODY;
        return;
    }

    if (t == "html") {
        if (!has_element_in_scope("body")) return;
        mode_ = InsertionMode::AFTER_BODY;
        return;
    }

    if (t == "br") {
        TagToken start = tag;
        start.type = TokenType::START_TAG;
        parse_generic_start_tag(start);
        return;
    }

    if (t == "p") {
        if (!has_element_in_scope("p")) return;
        generate_implied_end_tags({"p"});
        close_element("p");
        return;
    }

    if (t == "li") {
        if (!has_element_in_scope("li")) return;
        generate_implied_end_tags({"li"});
        close_element("li");
        return;
    }

    if (t == "dd" || t == "dt") {
        if (!has_element_in_scope(t)) return;
        generate_implied_end_tags({t});
        close_element(t);
        return;
    }

    if (is_heading_tag(t)) {
        if (!has_element_in_scope(t)) return;
        generate_implied_end_tags();
        close_element(t);
        return;
    }

    if (t == "a" || t == "b" || t == "big" || t == "code" || t == "em" || t == "font" ||
        t == "i" || t == "nobr" || t == "s" || t == "small" || t == "strike" ||
        t == "strong" || t == "tt" || t == "u") {
        adoption_agency_algorithm(t);
        return;
    }

    // SVG/MathML end tags: pop up to the matching element
    if (t == "svg" || t == "math") {
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] &&
                stack_[static_cast<u32>(i)]->tag_name == t) {
                stack_.resize(static_cast<u32>(i));
                break;
            }
        }
        // Check if we're still inside a foreign element on the stack
        foreign_ = false;
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] &&
                (stack_[static_cast<u32>(i)]->tag_name == "svg" ||
                 stack_[static_cast<u32>(i)]->tag_name == "math")) {
                foreign_ = true;
                break;
            }
        }
        return;
    }

    if (t == "div" || t == "section" || t == "nav" || t == "article" || t == "aside" ||
        t == "header" || t == "footer" || t == "main" || t == "address" || t == "blockquote" ||
        t == "center" || t == "details" || t == "dialog" || t == "dir" || t == "dl" ||
        t == "fieldset" || t == "figcaption" || t == "figure" || t == "hgroup" ||
        t == "listing" || t == "menu" || t == "pre" || t == "summary" || t == "ul" ||
        t == "ol") {
        if (!has_element_in_scope(t)) return;
        generate_implied_end_tags({t});
        close_element(t);
        return;
    }

    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (stack_[i] && stack_[i]->tag_name == t) {
            generate_implied_end_tags({t});
            stack_.resize(static_cast<u32>(i));
            return;
        }
    }
}

} // namespace browser::html
