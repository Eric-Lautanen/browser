#include "../layout.hpp"

#include <string>
#include <vector>

namespace browser::css {

    void LayoutEngine::layout_inline(LayoutNode *node, f32 containing_width, f32) {
        f32 char_width_factor = 0.6f;

        f32 parent_font_size = root_font_size_;
        if (node->parent) {
            auto *pfs = node->parent->style().get("font-size");
            if (pfs && pfs->type == CSSValue::Type::LENGTH && pfs->length.unit == Length::Unit::PX) {
                parent_font_size = pfs->length.value;
            }
        }
        f32 font_size = resolve_font_size(node->style(), parent_font_size);
        f32 char_width = char_width_factor * font_size;

        f32 line_height = font_size * 1.75f;
        auto *lh = node->style().get("line-height");
        if (lh && lh->type == CSSValue::Type::NUMBER && lh->number > 0) {
            line_height = font_size * lh->number;
        } else if (lh && lh->type == CSSValue::Type::LENGTH && lh->length.value > 0) {
            line_height = lh->length.value;
        }

        std::string text_align = "left";
        auto *ta = node->style().get("text-align");
        if (ta && ta->type == CSSValue::Type::KEYWORD) {
            text_align = ta->keyword;
        }

        std::string whitespace = "normal";
        auto *ws = node->style().get("white-space");
        if (ws && ws->type == CSSValue::Type::KEYWORD) {
            whitespace = ws->keyword;
        }

        auto *wb = node->style().get("word-break");
        bool break_words = (wb && wb->type == CSSValue::Type::KEYWORD && wb->keyword == "break-all");
        auto *ow = node->style().get("overflow-wrap");
        if (ow && ow->type == CSSValue::Type::KEYWORD && ow->keyword == "break-word")
            break_words = true;

        bool nowrap = (whitespace == "nowrap");
        bool preserve_ws = (whitespace == "pre" || whitespace == "pre-wrap" || whitespace == "pre-line");

        if (node->text().empty())
            return;

        struct Word {
            std::string text;
            f32 width;
        };
        std::vector<Word> words;
        std::string text = node->text();
        std::size_t start = 0;

        if (preserve_ws) {
            for (char c : text) {
                f32 w = text_measure_fn_ ? text_measure_fn_(text_measurer_ctx_, std::string(1, c), (u32)font_size)
                                         : char_width;
                words.push_back({std::string(1, c), w});
            }
        } else {
            while (start < text.size()) {
                if (text[start] == ' ' && whitespace != "pre") {
                    f32 sp_w =
                        text_measure_fn_ ? text_measure_fn_(text_measurer_ctx_, " ", (u32)font_size) : char_width;
                    words.push_back({" ", sp_w});
                    while (start < text.size() && text[start] == ' ') ++start;
                    continue;
                }
                if (start >= text.size())
                    break;
                std::size_t end = text.find(' ', start);
                if (end == std::string::npos)
                    end = text.size();
                std::string word_text = text.substr(start, end - start);
                f32 word_width = text_measure_fn_ ? text_measure_fn_(text_measurer_ctx_, word_text, (u32)font_size)
                                                  : static_cast<f32>(word_text.size()) * char_width;
                words.push_back({std::move(word_text), word_width});
                start = end;
            }
        }

        if (whitespace == "normal" && !words.empty() && words.back().text == " ") {
            words.pop_back();
        }

        f32 line_x = 0;
        f32 line_y = 0;
        f32 max_line_width = 0;

        for (auto &word : words) {
            if (word.text == " ") {
                line_x += word.width;
                continue;
            }
            if (line_x + word.width > containing_width && line_x > 0 && !nowrap) {
                if (break_words && word.width > containing_width) {
                }
                if (line_x > max_line_width)
                    max_line_width = line_x;
                line_y += line_height;
                line_x = 0;
            }
            line_x += word.width;
            if (line_x > max_line_width)
                max_line_width = line_x;
        }

        if (max_line_width > 0 && !words.empty() && words.back().text == " ") {
            max_line_width -= words.back().width;
        }
        f32 total_height = line_y + (line_x > 0 ? line_height : 0);

        node->content.width = text_align == "center"  ? containing_width
                              : text_align == "right" ? containing_width
                                                      : max_line_width;
        node->content.height = total_height;
    }

}  // namespace browser::css
