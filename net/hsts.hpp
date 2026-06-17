#pragma once
#include "../tests/utility.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace browser::net {

    struct HSTSEntry {
        std::string host;
        u64 expires_time = 0;
        bool include_subdomains = false;
    };

    class HSTSManager {
    public:
        HSTSManager();
        ~HSTSManager();

        void process_header(const std::string &host, const std::string &header_value);
        bool should_upgrade(const std::string &host) const;
        std::string upgrade_url(const std::string &url) const;

        void load_from_file(const std::string &path);
        void save_to_file(const std::string &path) const;

        void remove_expired();

        void load_preload_list();

    private:
        std::unordered_map<std::string, HSTSEntry> entries_;

        static u64 current_time_seconds();
    };

}  // namespace browser::net
