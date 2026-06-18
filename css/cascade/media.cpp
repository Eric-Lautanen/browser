#include "engine.hpp"

#include <cmath>
#include <cstdlib>
#include <regex>
#include <string>

namespace browser::css {

    bool evaluate_media_query(const std::string &prelude,
                              f32 viewport_width,
                              f32 viewport_height,
                              f32 device_pixel_ratio,
                              const std::string &color_scheme) {
        std::string lower;
        for (char c : prelude) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower.empty() || lower == "all")
            return true;

        bool negate = false;
        if (lower.substr(0, 4) == "not ") {
            negate = true;
            lower = lower.substr(4);
        }

        if (lower.substr(0, 5) == "only ") {
            lower = lower.substr(5);
        }

        auto eval_single = [&](const std::string &cond) -> bool {
            if (cond == "all")
                return true;
            if (cond == "screen")
                return true;
            if (cond == "print")
                return false;

            std::regex minw_re(R"(min-width\s*:\s*(\d+)\s*px)");
            std::smatch m;
            if (std::regex_search(cond, m, minw_re)) {
                f32 minw = std::stof(m[1].str());
                return viewport_width >= minw;
            }

            std::regex maxw_re(R"(max-width\s*:\s*(\d+)\s*px)");
            if (std::regex_search(cond, m, maxw_re)) {
                f32 maxw = std::stof(m[1].str());
                return viewport_width <= maxw;
            }

            std::regex minh_re(R"(min-height\s*:\s*(\d+)\s*px)");
            if (std::regex_search(cond, m, minh_re)) {
                f32 minh = std::stof(m[1].str());
                return viewport_height >= minh;
            }

            std::regex maxh_re(R"(max-height\s*:\s*(\d+)\s*px)");
            if (std::regex_search(cond, m, maxh_re)) {
                f32 maxh = std::stof(m[1].str());
                return viewport_height <= maxh;
            }

            std::regex w_re(R"(width\s*:\s*(\d+)\s*px)");
            if (std::regex_search(cond, m, w_re)) {
                f32 w = std::stof(m[1].str());
                return std::abs(viewport_width - w) < 0.5f;
            }

            std::regex h_re(R"(height\s*:\s*(\d+)\s*px)");
            if (std::regex_search(cond, m, h_re)) {
                f32 h = std::stof(m[1].str());
                return std::abs(viewport_height - h) < 0.5f;
            }

            if (cond.find("prefers-color-scheme") != std::string::npos) {
                if (cond.find("dark") != std::string::npos)
                    return color_scheme == "dark";
                if (cond.find("light") != std::string::npos)
                    return color_scheme == "light";
            }

            if (cond.find("prefers-reduced-motion") != std::string::npos) {
                if (cond.find("reduce") != std::string::npos)
                    return false;
                if (cond.find("no-preference") != std::string::npos)
                    return true;
            }

            if (cond.find("orientation") != std::string::npos) {
                if (cond.find("portrait") != std::string::npos)
                    return viewport_height > viewport_width;
                if (cond.find("landscape") != std::string::npos)
                    return viewport_width >= viewport_height;
            }

            std::regex res_re(R"(resolution:\s*(\d+(?:\.\d+)?)\s*dpi)");
            if (std::regex_search(cond, m, res_re)) {
                f32 res = std::stof(m[1].str());
                return device_pixel_ratio * 96.0f >= res;
            }

            if (cond.find("hover") != std::string::npos) {
                return true;
            }

            if (cond.find("pointer") != std::string::npos) {
                if (cond.find("fine") != std::string::npos)
                    return true;
                if (cond.find("coarse") != std::string::npos)
                    return false;
            }

            return true;
        };

        // Split on commas first (each comma-separated query is an OR)
        std::vector<std::string> or_parts;
        size_t pos = 0;
        while (pos < lower.size()) {
            size_t comma = lower.find(',', pos);
            if (comma == std::string::npos) {
                or_parts.push_back(lower.substr(pos));
                break;
            }
            or_parts.push_back(lower.substr(pos, comma - pos));
            pos = comma + 1;
        }

        for (const auto &part : or_parts) {
            bool part_result = true;
            size_t p = 0;
            bool first_cond = true;
            while (p < part.size()) {
                while (p < part.size() && (part[p] == ' ' || part[p] == '(' || part[p] == ')')) p++;
                if (p >= part.size())
                    break;
                // Handle "or" between conditions
                if (part.substr(p, 3) == "or ") {
                    p += 3;
                    if (first_cond) { part_result = false; first_cond = true; }
                    continue;
                }
                size_t and_pos = part.find(" and ", p);
                std::string cond;
                if (and_pos == std::string::npos) {
                    cond = part.substr(p);
                    p = part.size();
                } else {
                    cond = part.substr(p, and_pos - p);
                    p = and_pos + 5;
                }
                while (!cond.empty() && (cond.back() == ' ' || cond.back() == ')')) cond.pop_back();
                while (!cond.empty() && (cond[0] == ' ' || cond[0] == '(')) cond = cond.substr(1);

                if (!cond.empty() && cond != "and" && cond != "or") {
                    if (first_cond) {
                        part_result = eval_single(cond);
                        first_cond = false;
                    } else {
                        part_result = part_result && eval_single(cond);
                    }
                }
            }
            if (part_result) {
                return negate ? false : true;
            }
        }

        return negate ? true : false;
    }

}  // namespace browser::css
