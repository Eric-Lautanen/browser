#pragma once
#include <string>
#include "css_values.hpp"
#include "tokenizer.hpp"

namespace browser::css {

class CssParser {
public:
    CssParser(const std::string& input);
    StyleSheet parse();

private:
    CssTokenizer tokenizer_;
    CssToken current_;
    void advance();
    void expect(CssTokenType type);
    Rule parse_rule();
    Selector parse_selector();
    Declaration parse_declaration();
    SimpleSelector parse_simple_selector();
    CSSValue parse_value();
    AtRule parse_at_rule();
    bool is_simple_selector_start() const;
};

inline StyleSheet parse(const std::string& css) {
    CssParser p(css);
    return p.parse();
}

}
