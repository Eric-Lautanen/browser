#pragma once
#include <string>

namespace browser {

    inline std::string escape_pipe(const std::string &s) {
        std::string r;
        for (char c : s) {
            if (c == '|')
                r += "\\p";
            else
                r += c;
        }
        return r;
    }

    inline std::string unescape_pipe(const std::string &s) {
        std::string r;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'p') {
                r += '|';
                i++;
            } else {
                r += s[i];
            }
        }
        return r;
    }

}  // namespace browser
