#include "../../html/utf8.hpp"
#include "../layout.hpp"

#include <string>
#include <vector>

namespace browser::css {

    namespace {
        u32 count_codepoints(const std::string &s) {
            u32 n = 0;
            for (char c : s) {
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                    n++;
            }
            return n;
        }
    }  // namespace

    void LayoutEngine::layout_inline(LayoutNode *node, f32 containing_width, f32) {
        f32 char_width_factor = 0.6f;

        f32 parent_font_size = root_font_size_;
        if (node->parent) {
            parent_font_size = resolve_font_size(node->parent->style(), root_font_size_);
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
        auto *ow = node->style().get("overflow-wrap");
        bool break_words = (wb && wb->type == CSSValue::Type::KEYWORD && wb->keyword == "break-all") ||
                           (ow && ow->type == CSSValue::Type::KEYWORD && ow->keyword == "break-word");

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
            const u8 *txt = reinterpret_cast<const u8 *>(text.data());
            u32 off = 0;
            u32 tlen = static_cast<u32>(text.size());
            while (off < tlen) {
                auto dr = browser::html::decode_utf8(txt + off, tlen - off);
                if (dr.bytes_consumed == 0) {
                    off++;
                    continue;
                }
                std::string ch(text.data() + off, dr.bytes_consumed);
                off += dr.bytes_consumed;
                f32 w = text_measure_fn_ ? text_measure_fn_(text_measurer_ctx_, ch, (u32)font_size) : char_width;
                words.push_back({std::move(ch), w});
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
                                                  : static_cast<f32>(count_codepoints(word_text)) * char_width;
                words.push_back({std::move(word_text), word_width});
                start = end;
            }
        }

        f32 line_x = 0;
        f32 line_y = 0;
        f32 max_line_width = 0;
        size_t line_word_start = 0;
        f32 pending_space = 0;

        auto flush_line = [&](size_t word_start, size_t word_end) {
            f32 line_width = line_x + pending_space;
            if (line_width > max_line_width)
                max_line_width = line_width;
            std::string line_str;
            for (size_t wi = word_start; wi < word_end; wi++) {
                if (words[wi].text == " ") {
                    line_str += ' ';
                } else {
                    line_str += words[wi].text;
                }
            }
            // Collapse multiple spaces to one for normal whitespace mode
            if (whitespace == "normal" || whitespace == "nowrap") {
                std::string collapsed;
                bool last_space = false;
                for (char c : line_str) {
                    if (c == ' ') {
                        if (!last_space)
                            collapsed += ' ';
                        last_space = true;
                    } else {
                        collapsed += c;
                        last_space = false;
                    }
                }
                line_str = std::move(collapsed);
            }
            node->text_lines.push_back({std::move(line_str), line_y});
            line_y += line_height;
            line_x = 0;
            pending_space = 0;
        };
        for (size_t wi = 0; wi < words.size(); wi++) {
            auto &word = words[wi];
            if (word.text == " ") {
                pending_space += word.width;
                continue;
            }
            f32 word_x = line_x + pending_space;
            if (word_x + word.width > containing_width && word_x > 0 && !nowrap) {
                if (break_words && word.width > containing_width) {
                    // Word is wider than container and break-word is set
                    // Fall through to place it on its own line (clipped by overflow)
                }
                flush_line(line_word_start, wi);
                line_word_start = wi;
                pending_space = 0;
                line_x = 0;
            } else {
                line_x = word_x;
                pending_space = 0;
            }
            line_x += word.width;
        }

        // Flush remaining words on the last line
        if (line_word_start < words.size()) {
            line_x += pending_space;
            flush_line(line_word_start, words.size());
        }

        f32 total_height = line_y;

        node->content.width = text_align == "center"  ? containing_width
                              : text_align == "right" ? containing_width
                                                      : max_line_width;
        node->content.height = total_height;
    }

}  // namespace browser::css
