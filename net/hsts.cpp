#include "hsts.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
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

    HSTSManager::HSTSManager() = default;
    HSTSManager::~HSTSManager() = default;

    u64 HSTSManager::current_time_seconds() {
        return static_cast<u64>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    void HSTSManager::process_header(const std::string &host, const std::string &header_value) {
        std::string lc_host = to_lower(host);
        std::string val = header_value;
        std::string max_age_str;
        bool include_subdomains = false;

        size_t pos = 0;
        while (pos < val.size()) {
            while (pos < val.size() && val[pos] == ' ') pos++;
            if (pos >= val.size())
                break;
            size_t semi = val.find(';', pos);
            std::string part = (semi == std::string::npos) ? val.substr(pos) : val.substr(pos, semi - pos);
            pos = (semi == std::string::npos) ? val.size() : semi + 1;
            part = trim(part);
            if (part.empty())
                continue;

            size_t eq = part.find('=');
            std::string attr_name, attr_val;
            if (eq != std::string::npos) {
                attr_name = to_lower(trim(part.substr(0, eq)));
                attr_val = trim(part.substr(eq + 1));
            } else {
                attr_name = to_lower(trim(part));
            }

            if (attr_name == "max-age") {
                max_age_str = attr_val;
            } else if (attr_name == "includesubdomains") {
                include_subdomains = true;
            }
        }

        if (max_age_str.empty())
            return;

        char *end = nullptr;
        long max_age = std::strtol(max_age_str.c_str(), &end, 10);
        if (end == max_age_str.c_str())
            return;

        auto now = current_time_seconds();
        if (max_age <= 0) {
            entries_.erase(lc_host);
            return;
        }

        HSTSEntry entry;
        entry.host = lc_host;
        entry.expires_time = now + static_cast<u64>(max_age);
        entry.include_subdomains = include_subdomains;
        entries_[lc_host] = entry;
    }

    bool HSTSManager::should_upgrade(const std::string &host) const {
        std::string lc_host = to_lower(host);
        auto it = entries_.find(lc_host);
        if (it != entries_.end()) {
            if (current_time_seconds() < it->second.expires_time) {
                return true;
            }
        }
        for (auto &[h, e] : entries_) {
            if (e.include_subdomains && current_time_seconds() < e.expires_time) {
                if (lc_host.size() > h.size() && lc_host.substr(lc_host.size() - h.size()) == h &&
                    lc_host[lc_host.size() - h.size() - 1] == '.') {
                    return true;
                }
            }
        }
        return false;
    }

    std::string HSTSManager::upgrade_url(const std::string &url) const {
        if (url.rfind("http://", 0) == 0) {
            std::string host_part = url.substr(7);
            size_t slash = host_part.find('/');
            std::string host = (slash == std::string::npos) ? host_part : host_part.substr(0, slash);
            if (should_upgrade(host)) {
                return "https://" + host_part;
            }
        }
        return url;
    }

    void HSTSManager::load_from_file(const std::string &path) {
        std::ifstream f(path);
        if (!f.is_open())
            return;
        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream ss(line);
            std::string host, expires_str, subdomains_str;
            if (!(ss >> host >> expires_str >> subdomains_str))
                continue;
            char *end = nullptr;
            u64 expires = static_cast<u64>(std::strtoull(expires_str.c_str(), &end, 10));
            if (end == expires_str.c_str())
                continue;
            HSTSEntry entry;
            entry.host = host;
            entry.expires_time = expires;
            entry.include_subdomains = (subdomains_str == "1");
            entries_[host] = entry;
        }
    }

    void HSTSManager::save_to_file(const std::string &path) const {
        std::ofstream f(path);
        if (!f.is_open())
            return;
        auto now = current_time_seconds();
        for (auto &[host, entry] : entries_) {
            if (entry.expires_time <= now)
                continue;
            f << entry.host << " " << entry.expires_time << " " << (entry.include_subdomains ? "1" : "0") << "\n";
        }
    }

    void HSTSManager::remove_expired() {
        auto now = current_time_seconds();
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.expires_time <= now) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void HSTSManager::load_preload_list() {
        static const struct {
            const char *host;
            bool include_subdomains;
        } preload[] = {
            {"google.com", true},
            {"youtube.com", true},
            {"facebook.com", true},
            {"twitter.com", true},
            {"github.com", true},
            {"microsoft.com", true},
            {"apple.com", true},
            {"amazon.com", true},
            {"wikipedia.org", true},
            {"mozilla.org", true},
            {"accounts.google.com", false},
            {"mail.google.com", false},
            {"drive.google.com", false},
            {"maps.google.com", false},
            {"paypal.com", true},
            {"stackoverflow.com", true},
            {"medium.com", true},
            {"cloudflare.com", true},
            {"dropbox.com", true},
            {"linkedin.com", true},
            {"reddit.com", true},
            {"whatsapp.com", true},
            {"instagram.com", true},
            {"netflix.com", true},
            {"adobe.com", true},
            {"wordpress.org", true},
            {"bing.com", true},
            {"office.com", true},
            {"live.com", true},
            {"outlook.com", true},
            {"godaddy.com", true},
            {"namecheap.com", true},
        };
        auto now = current_time_seconds();
        u64 far_future = now + 365 * 24 * 3600 * 10;
        for (auto &p : preload) {
            if (entries_.find(p.host) == entries_.end()) {
                HSTSEntry entry;
                entry.host = p.host;
                entry.expires_time = far_future;
                entry.include_subdomains = p.include_subdomains;
                entries_[p.host] = entry;
            }
        }
    }

}  // namespace browser::net
