#include "registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <windows.h>

namespace browser::render {

    static const char *kFontsDir = "C:\\Windows\\Fonts\\";
    static const char *kRegKey = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";

    FontRegistry &FontRegistry::instance() {
        static FontRegistry reg;
        return reg;
    }

    std::string FontRegistry::normalize(const std::string &name) const {
        std::string r;
        r.reserve(name.size());
        bool in_paren = false;
        for (char c : name) {
            if (c == '(') {
                in_paren = true;
                continue;
            }
            if (c == ')') {
                in_paren = false;
                continue;
            }
            if (!in_paren)
                r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        // Trim trailing whitespace
        while (!r.empty() && r.back() == ' ') r.pop_back();
        // Collapse internal spaces
        std::string out;
        bool last_space = false;
        for (char c : r) {
            if (c == ' ') {
                if (!last_space)
                    out += c;
                last_space = true;
            } else {
                out += c;
                last_space = false;
            }
        }
        return out;
    }

    void FontRegistry::scan() {
        if (scanned_)
            return;
        scanned_ = true;

        HKEY hkey;
        LONG ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, kRegKey, 0, KEY_READ, &hkey);
        if (ret != ERROR_SUCCESS)
            return;

        char value_name[1024];
        char data[1024];
        DWORD value_name_sz, data_sz, type;
        DWORD index = 0;

        while (true) {
            value_name_sz = sizeof(value_name);
            data_sz = sizeof(data);
            type = 0;
            ret = RegEnumValueA(
                hkey, index, value_name, &value_name_sz, nullptr, &type, reinterpret_cast<LPBYTE>(data), &data_sz);
            if (ret != ERROR_SUCCESS)
                break;

            if (type == REG_SZ && value_name_sz > 0) {
                std::string name(value_name, value_name_sz - 1);  // exclude null
                std::string path = kFontsDir + std::string(data, data_sz - 1);
                std::string key = normalize(name);
                if (!key.empty() && !path.empty()) {
                    // Only keep the first (most specific) mapping for each normalized name
                    if (fonts_.find(key) == fonts_.end())
                        fonts_[key] = path;
                }
            }
            index++;
        }

        RegCloseKey(hkey);

        // Generic family mapping
        generics_["sans-serif"] = "Arial";
        generics_["serif"] = "Times New Roman";
        generics_["monospace"] = "Consolas";
        generics_["cursive"] = "Comic Sans MS";
        generics_["fantasy"] = "Gabriola";
        generics_["system-ui"] = "Segoe UI";
    }

    std::string FontRegistry::find_path(const std::string &family_name) const {
        std::string key = normalize(family_name);

        // Direct lookup
        auto it = fonts_.find(key);
        if (it != fonts_.end())
            return it->second;

        // Try partial match — some registry entries have extra qualifiers
        // e.g. "MS Gothic (TrueType)" matches lookup for "ms gothic"
        for (const auto &[reg_name, path] : fonts_) {
            if (reg_name.find(key) != std::string::npos || key.find(reg_name) != std::string::npos) {
                return path;
            }
        }

        return {};
    }

    std::string FontRegistry::find_generic(const std::string &generic) const {
        std::string key;
        for (char c : generic) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        auto it = generics_.find(key);
        if (it != generics_.end()) {
            std::string path = find_path(it->second);
            if (!path.empty())
                return path;
            // Fallback generic names that should always work
            if (key == "sans-serif")
                return std::string(kFontsDir) + "arial.ttf";
            if (key == "serif")
                return std::string(kFontsDir) + "times.ttf";
            if (key == "monospace")
                return std::string(kFontsDir) + "consola.ttf";
        }
        return {};
    }

}  // namespace browser::render