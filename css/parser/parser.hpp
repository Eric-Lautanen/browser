#pragma once
#include "async/executor.hpp"
#include "async/task.hpp"
#include "css/css_values.hpp"
#include "css/tokenizer.hpp"

#include <string>
#include <vector>

namespace browser::css {

    class CssParser {
    public:
        CssParser(const std::string &input);
        StyleSheet parse();
        std::vector<Declaration> parse_inline_declarations();
        static std::vector<Selector> parse_selectors(const std::string &input);

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
        CSSValue parse_transform_func(const std::string &func_name);
        CSSValue parse_gradient(const std::string &func_name);
        AtRule parse_at_rule();
        KeyframesRule parse_keyframes();
        bool parse_keyframe_block(KeyframeBlock &block);
        bool is_simple_selector_start() const;
        std::string consume_function_body();
    };

    inline async::task<StyleSheet> parse_async(const std::string &css) {
        co_await async::thread_pool_executor{};
        CssParser p(css);
        co_return p.parse();
    }

    // Property name recognition and shorthand expansion
    bool is_shorthand_property(const std::string &name);
    std::vector<std::string> expand_shorthand(const std::string &name);

}  // namespace browser::css
