#pragma once
#include "../core/utility.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace browser::net {

    struct CSPSource {
        enum Type { SELF, NONE, UNSAFE_INLINE, UNSAFE_EVAL, STRICT_DYNAMIC, SCHEME, HOST, WILDCARD_HOST };
        Type type = SELF;
        std::string scheme;
        std::string host;
        u16 port = 0;
        bool is_https = false;
    };

    struct CSPDirective {
        std::string name;
        std::vector<CSPSource> sources;
    };

    struct CSPPolicy {
        std::vector<CSPDirective> directives;
        bool report_only = false;
        std::string report_uri;
        // Origin of the page that delivered this policy — 'self' resolves
        // against it (N-S4). Empty means unknown: 'self' matches nothing.
        std::string self_origin;

        bool allows(const std::string &directive_name, const std::string &origin) const;
        bool allows_inline_script() const;
        bool allows_eval() const;
        bool allows_inline_style() const;

        bool has_directive(const std::string &name) const;

    private:
        bool source_matches(const CSPSource &source, const std::string &origin) const;
    };

    class CSPParser {
    public:
        static CSPPolicy parse(const std::string &header_value, const std::string &page_origin = "");
    };

}  // namespace browser::net
