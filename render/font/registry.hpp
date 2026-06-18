#pragma once
#include <string>
#include <unordered_map>

namespace browser::render {

    class FontRegistry {
    public:
        static FontRegistry &instance();

        void scan();

        std::string find_path(const std::string &family_name) const;
        std::string find_generic(const std::string &generic) const;

        bool scanned() const { return scanned_; }

    private:
        FontRegistry() = default;

        std::string normalize(const std::string &name) const;

        bool scanned_ = false;
        std::unordered_map<std::string, std::string> fonts_;
        // Generic CSS family → concrete name mapping
        std::unordered_map<std::string, std::string> generics_;
    };

}  // namespace browser::render