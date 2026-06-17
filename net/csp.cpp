#include "csp.hpp"

#include "origin.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace browser::net {

    static std::string trim(const std::string &s) {
        size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
        size_t end = s.size();
        while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
        return s.substr(start, end - start);
    }

    static std::string to_lower(const std::string &s) {
        std::string r(s.size(), 0);
        for (size_t i = 0; i < s.size(); i++) r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        return r;
    }

    static CSPSource parse_source(const std::string &src) {
        CSPSource s;
        std::string t = trim(src);
        if (t == "'self'") {
            s.type = CSPSource::SELF;
        } else if (t == "'none'") {
            s.type = CSPSource::NONE;
        } else if (t == "'unsafe-inline'") {
            s.type = CSPSource::UNSAFE_INLINE;
        } else if (t == "'unsafe-eval'") {
            s.type = CSPSource::UNSAFE_EVAL;
        } else if (t == "'strict-dynamic'") {
            s.type = CSPSource::STRICT_DYNAMIC;
        } else if (t.rfind("http://", 0) == 0) {
            s.type = CSPSource::SCHEME;
            s.scheme = "http";
        } else if (t.rfind("https://", 0) == 0) {
            s.type = CSPSource::SCHEME;
            s.scheme = "https";
        } else if (t.rfind("*.", 0) == 0) {
            s.type = CSPSource::WILDCARD_HOST;
            s.host = t.substr(2);
        } else {
            size_t colon = t.find(':');
            if (colon != std::string::npos) {
                s.type = CSPSource::HOST;
                s.host = t.substr(0, colon);
                std::string port_str = t.substr(colon + 1);
                char *end = nullptr;
                long p = std::strtol(port_str.c_str(), &end, 10);
                if (end != port_str.c_str() && p > 0 && p <= 65535)
                    s.port = static_cast<u16>(p);
            } else {
                s.type = CSPSource::HOST;
                s.host = t;
            }
        }
        return s;
    }

    CSPPolicy CSPParser::parse(const std::string &header_value) {
        CSPPolicy policy;
        std::string h = to_lower(header_value);
        size_t pos = 0;
        while (pos < h.size()) {
            while (pos < h.size() && (h[pos] == ' ' || h[pos] == '\t' || h[pos] == ';')) pos++;
            if (pos >= h.size())
                break;

            size_t semi = h.find(';', pos);
            std::string part = (semi == std::string::npos) ? h.substr(pos) : h.substr(pos, semi - pos);
            pos = (semi == std::string::npos) ? h.size() : semi + 1;

            part = trim(part);
            if (part.empty())
                continue;

            size_t space = part.find(' ');
            if (space == std::string::npos)
                continue;

            std::string dir_name = part.substr(0, space);
            std::string sources_str = part.substr(space + 1);

            if (dir_name == "report-uri") {
                policy.report_uri = trim(sources_str);
                continue;
            }
            if (dir_name == "report-to") {
                continue;
            }

            CSPDirective dir;
            dir.name = dir_name;

            size_t sp = 0;
            while (sp < sources_str.size()) {
                while (sp < sources_str.size() && (sources_str[sp] == ' ' || sources_str[sp] == '\t')) sp++;
                if (sp >= sources_str.size())
                    break;
                size_t next = sources_str.find(' ', sp);
                std::string src = sources_str.substr(sp, next - sp);
                sp = (next == std::string::npos) ? sources_str.size() : next + 1;
                dir.sources.push_back(parse_source(src));
            }

            policy.directives.push_back(std::move(dir));
        }
        return policy;
    }

    static const CSPDirective *find_directive(const CSPPolicy &policy, const std::string &name) {
        for (auto &d : policy.directives) {
            if (d.name == name)
                return &d;
        }
        return nullptr;
    }

    bool CSPPolicy::has_directive(const std::string &name) const {
        return find_directive(*this, name) != nullptr;
    }

    bool CSPPolicy::source_matches(const CSPSource &source, const std::string &origin) const {
        switch (source.type) {
            case CSPSource::SELF:
                return true;
            case CSPSource::NONE:
                return false;
            case CSPSource::UNSAFE_INLINE:
            case CSPSource::UNSAFE_EVAL:
            case CSPSource::STRICT_DYNAMIC:
                return false;
            case CSPSource::SCHEME: {
                auto o = Origin::from_url_str(origin);
                return o.scheme == source.scheme;
            }
            case CSPSource::HOST: {
                auto o = Origin::from_url_str(origin);
                if (o.host == source.host)
                    return true;
                if (source.port != 0 && o.port != source.port)
                    return false;
                return false;
            }
            case CSPSource::WILDCARD_HOST: {
                auto o = Origin::from_url_str(origin);
                if (o.host.size() > source.host.size() &&
                    o.host.substr(o.host.size() - source.host.size()) == source.host) {
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    bool CSPPolicy::allows(const std::string &directive_name, const std::string &origin) const {
        auto *dir = find_directive(*this, directive_name);
        if (!dir) {
            auto *def = find_directive(*this, "default-src");
            if (!def)
                return true;
            if (def->sources.empty())
                return false;
            for (auto &src : def->sources) {
                if (source_matches(src, origin))
                    return true;
            }
            return false;
        }
        if (dir->sources.empty())
            return false;
        for (auto &src : dir->sources) {
            if (source_matches(src, origin))
                return true;
        }
        return false;
    }

    bool CSPPolicy::allows_inline_script() const {
        auto *dir = find_directive(*this, "script-src");
        if (!dir) {
            auto *def = find_directive(*this, "default-src");
            if (!def)
                return true;
            for (auto &src : def->sources) {
                if (src.type == CSPSource::UNSAFE_INLINE)
                    return true;
            }
            return false;
        }
        for (auto &src : dir->sources) {
            if (src.type == CSPSource::UNSAFE_INLINE)
                return true;
        }
        return false;
    }

    bool CSPPolicy::allows_eval() const {
        auto *dir = find_directive(*this, "script-src");
        if (!dir) {
            auto *def = find_directive(*this, "default-src");
            if (!def)
                return true;
            for (auto &src : def->sources) {
                if (src.type == CSPSource::UNSAFE_EVAL)
                    return true;
            }
            return false;
        }
        for (auto &src : dir->sources) {
            if (src.type == CSPSource::UNSAFE_EVAL)
                return true;
        }
        return false;
    }

    bool CSPPolicy::allows_inline_style() const {
        auto *dir = find_directive(*this, "style-src");
        if (!dir) {
            auto *def = find_directive(*this, "default-src");
            if (!def)
                return true;
            for (auto &src : def->sources) {
                if (src.type == CSPSource::UNSAFE_INLINE)
                    return true;
            }
            return false;
        }
        for (auto &src : dir->sources) {
            if (src.type == CSPSource::UNSAFE_INLINE)
                return true;
        }
        return false;
    }

}  // namespace browser::net
