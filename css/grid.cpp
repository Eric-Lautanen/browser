#include "grid.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace browser::css {

    static std::vector<std::string> split_whitespace(const std::string &s) {
        std::vector<std::string> tokens;
        std::string cur;
        u32 paren_depth = 0;
        for (std::size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '(') {
                paren_depth++;
                cur += c;
            } else if (c == ')') {
                paren_depth--;
                cur += c;
            } else if (paren_depth == 0 && (c == ' ' || c == '\t' || c == '\n')) {
                if (!cur.empty()) {
                    tokens.push_back(cur);
                    cur.clear();
                }
            } else {
                cur += c;
            }
        }
        if (!cur.empty())
            tokens.push_back(cur);
        return tokens;
    }

    static std::vector<std::string> split_slash(const std::string &s) {
        std::vector<std::string> parts;
        std::string cur;
        for (std::size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '/') {
                if (!cur.empty())
                    parts.push_back(cur);
                cur.clear();
                // skip whitespace after slash
                while (i + 1 < s.size() && (s[i + 1] == ' ' || s[i + 1] == '\t')) i++;
            } else {
                cur += c;
            }
        }
        if (!cur.empty())
            parts.push_back(cur);
        return parts;
    }

    static std::string trim(const std::string &s) {
        std::size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
        std::size_t end = s.size();
        while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
        return s.substr(start, end - start);
    }

    static bool ends_with(const std::string &s, const std::string &suffix) {
        if (s.size() < suffix.size())
            return false;
        return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    static bool starts_with(const std::string &s, const std::string &prefix) {
        if (s.size() < prefix.size())
            return false;
        return s.compare(0, prefix.size(), prefix) == 0;
    }

    static f32 parse_length_value(const std::string &token, f32 container_size, f32 font_size, f32 root_font_size) {
        if (token.empty())
            return 0;

        // Find where the number ends and the unit begins
        std::size_t num_end = 0;
        bool neg = false;
        if (token[0] == '-') {
            neg = true;
            num_end = 1;
        }
        while (num_end < token.size() &&
               (std::isdigit(static_cast<unsigned char>(token[num_end])) || token[num_end] == '.')) {
            num_end++;
        }

        f32 val = 0;
        if (num_end > (neg ? 1 : 0)) {
            val = std::strtof(token.substr(0, num_end).c_str(), nullptr);
        }
        if (neg)
            val = -val;

        std::string unit = token.substr(num_end);
        std::string unit_lower;
        for (char c : unit) unit_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (unit_lower == "px")
            return val;
        if (unit_lower == "em")
            return val * font_size;
        if (unit_lower == "rem")
            return val * root_font_size;
        if (unit_lower == "%")
            return val / 100.0f * container_size;
        if (unit_lower == "vw")
            return val / 100.0f * container_size;
        if (unit_lower == "vh")
            return val / 100.0f * container_size;
        return val;  // unitless
    }

    static GridTrackDef parse_single_track(const std::string &token,
                                           f32 container_size,
                                           f32 font_size,
                                           f32 root_font_size) {
        GridTrackDef def;

        if (token == "auto") {
            def.type = GridTrackType::AUTO;
            def.min_size = Length{0, Length::Unit::PX};
            def.max_size = Length{0, Length::Unit::PX};
            return def;
        }

        if (starts_with(token, "minmax(") && token.back() == ')') {
            def.type = GridTrackType::MINMAX;
            std::string inner = token.substr(7, token.size() - 8);
            // Find the comma separating min and max
            std::size_t comma = std::string::npos;
            i32 depth = 0;
            for (std::size_t i = 0; i < inner.size(); i++) {
                if (inner[i] == '(')
                    depth++;
                else if (inner[i] == ')')
                    depth--;
                else if (inner[i] == ',' && depth == 0) {
                    comma = i;
                    break;
                }
            }
            std::string min_str = trim(inner.substr(0, comma));
            std::string max_str = trim(inner.substr(comma + 1));

            if (min_str == "auto") {
                def.min_size = Length{0, Length::Unit::PX};
            } else {
                def.min_size.value = parse_length_value(min_str, container_size, font_size, root_font_size);
                def.min_size.unit = Length::Unit::PX;
            }

            if (ends_with(max_str, "fr")) {
                def.max_size.value = parse_length_value(max_str, container_size, font_size, root_font_size);
                def.max_size.unit = Length::Unit::NONE;
            } else {
                def.max_size.value = parse_length_value(max_str, container_size, font_size, root_font_size);
                def.max_size.unit = Length::Unit::PX;
            }
            return def;
        }

        if (starts_with(token, "fit-content(") && token.back() == ')') {
            def.type = GridTrackType::FIT_CONTENT;
            std::string inner = trim(token.substr(12, token.size() - 13));
            def.min_size = Length{0, Length::Unit::PX};
            def.max_size.value = parse_length_value(inner, container_size, font_size, root_font_size);
            def.max_size.unit = Length::Unit::PX;
            return def;
        }

        if (ends_with(token, "fr")) {
            def.type = GridTrackType::FLEX;
            f32 fr_val = parse_length_value(token, container_size, font_size, root_font_size);
            def.min_size = Length{fr_val, Length::Unit::NONE};
            def.max_size = Length{fr_val, Length::Unit::NONE};
            return def;
        }

        // Fixed length
        def.type = GridTrackType::FIXED;
        def.min_size.value = parse_length_value(token, container_size, font_size, root_font_size);
        def.min_size.unit = Length::Unit::PX;
        def.max_size = def.min_size;
        return def;
    }

    std::vector<GridTrackDef> parse_track_list(std::string css_value,
                                               f32 container_size,
                                               f32 font_size,
                                               f32 root_font_size) {
        // Merge number+unit pairs (e.g., "1 fr" -> "1fr", "3 . 5 px" -> "3.5px")
        std::string merged;
        {
            auto tokens = split_whitespace(css_value);
            for (size_t i = 0; i < tokens.size(); i++) {
                if (i + 1 < tokens.size()) {
                    char *end = nullptr;
                    std::strtod(tokens[i].c_str(), &end);
                    if (end && *end == '\0' && !tokens[i].empty()) {
                        const std::string &next = tokens[i + 1];
                        if (next == "fr" || next == "px" || next == "em" || next == "rem" || next == "%" ||
                            next == "vw" || next == "vh" || next == "deg") {
                            merged += tokens[i] + tokens[i + 1];
                            i++;
                            continue;
                        }
                    }
                }
                if (!merged.empty())
                    merged += " ";
                merged += tokens[i];
            }
        }
        css_value = std::move(merged);

        auto tokens = split_whitespace(css_value);
        std::vector<GridTrackDef> tracks;
        for (const auto &tok : tokens) {
            // Handle repeat(N, ...)
            if (starts_with(tok, "repeat(") && tok.back() == ')') {
                std::string inner = tok.substr(7, tok.size() - 8);
                // Find comma separating count from track pattern
                auto comma = inner.find(',');
                if (comma != std::string::npos) {
                    std::string count_str = trim(inner.substr(0, comma));
                    std::string pattern_str = trim(inner.substr(comma + 1));
                    i32 count = static_cast<i32>(std::strtol(count_str.c_str(), nullptr, 10));
                    if (count > 0 && count <= 1000) {
                        // Parse pattern tokens (may be space-separated or comma-separated)
                        // First try comma separation for "repeat(3, 1fr 2fr)" vs "repeat(3, 1fr, 2fr)"
                        std::vector<std::string> pattern_tokens;
                        {
                            std::string cur;
                            u32 paren_depth = 0;
                            for (size_t i = 0; i < pattern_str.size(); i++) {
                                char c = pattern_str[i];
                                if (c == '(') {
                                    paren_depth++;
                                    cur += c;
                                } else if (c == ')') {
                                    paren_depth--;
                                    cur += c;
                                } else if (paren_depth == 0 && (c == ' ' || c == '\t')) {
                                    if (!cur.empty()) {
                                        pattern_tokens.push_back(cur);
                                        cur.clear();
                                    }
                                } else {
                                    if (c != ',')
                                        cur += c;
                                }
                            }
                            if (!cur.empty())
                                pattern_tokens.push_back(cur);
                        }
                        if (!pattern_tokens.empty()) {
                            for (i32 r = 0; r < count; r++) {
                                for (const auto &pt : pattern_tokens) {
                                    tracks.push_back(parse_single_track(pt, container_size, font_size, root_font_size));
                                }
                            }
                        }
                    }
                }
            } else {
                tracks.push_back(parse_single_track(tok, container_size, font_size, root_font_size));
            }
        }
        return tracks;
    }

    GridPlacement parse_grid_line(const std::string &value) {
        GridPlacement p;
        std::string v = trim(value);
        if (v.empty())
            return p;

        auto parts = split_slash(v);

        auto parse_part = [](const std::string &part, i32 &line, u32 &span, bool &is_explicit) {
            std::string s = trim(part);
            if (s.empty() || s == "auto") {
                line = 0;
                return;
            }
            if (starts_with(s, "span ")) {
                std::string num_str = trim(s.substr(5));
                span = static_cast<u32>(std::strtol(num_str.c_str(), nullptr, 10));
                line = 0;
                is_explicit = true;
                return;
            }
            // plain number
            line = static_cast<i32>(std::strtol(s.c_str(), nullptr, 10));
            is_explicit = true;
        };

        if (parts.size() == 1) {
            // Single value: either a line number or "span N"
            parse_part(parts[0], p.line_start, p.span, p.is_explicit);
            p.line_end = p.line_start;
        } else if (parts.size() >= 2) {
            // Two parts: start / end
            u32 span_dummy = 1;
            parse_part(parts[0], p.line_start, span_dummy, p.is_explicit);
            parse_part(parts[1], p.line_end, p.span, p.is_explicit);
            // If line_end is set and span is still default, derive span from line_end - line_start
            if (p.line_end > 0 && p.span == 1 && p.line_start > 0) {
                i32 diff = p.line_end - p.line_start;
                p.span = static_cast<u32>(diff > 0 ? diff : 1);
            }
        }

        return p;
    }

}  // namespace browser::css
