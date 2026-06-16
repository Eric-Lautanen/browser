#pragma once
#include <string>
#include "css_values.hpp"
#include "tokenizer.hpp"
#include "../async/task.hpp"
#include "../async/executor.hpp"

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
    CSSValue parse_calc_args();
    CSSValue parse_transform_func(const std::string& func_name);
    CSSValue parse_gradient(const std::string& func_name);
    AtRule parse_at_rule();
    KeyframesRule parse_keyframes();
    bool parse_keyframe_block(KeyframeBlock& block);
    bool is_simple_selector_start() const;
    std::string consume_function_body();
};

inline async::task<StyleSheet> parse_async(const std::string& css) {
    co_await async::thread_pool_executor{};
    CssParser p(css);
    co_return p.parse();
}

}
