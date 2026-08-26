#pragma once
#include "../async/task.hpp"
#include "socket.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace browser::net {

    // N-P7: process-wide TTL cache for resolved hostnames. A browser session
    // resolves the same handful of hosts over and over (page + CSS + images);
    // caching removes a full UDP round trip per resource. Thread-safe: lookups
    // run on pool threads. Deliberate process-wide service, like IOCP::global().
    class DnsCache {
    public:
        // Returns cached addresses if present and unexpired.
        std::optional<std::vector<IPv4Address>> lookup(const std::string &hostname);
        // Stores addresses with the given TTL, clamped to sane bounds.
        void store(const std::string &hostname, const std::vector<IPv4Address> &addrs, u32 ttl_seconds);

        void clear();

    private:
        struct Entry {
            std::vector<IPv4Address> addrs;
            std::chrono::steady_clock::time_point expires;
        };
        std::mutex mutex_;
        std::unordered_map<std::string, Entry> entries_;

        static constexpr size_t kMaxEntries = 256;
    };

    DnsCache &global_dns_cache();

    class DNSResolver {
    public:
        DNSResolver();
        explicit DNSResolver(const IPv4Address &dns_server);
        ~DNSResolver();
        DNSResolver(DNSResolver &&) noexcept = default;
        DNSResolver &operator=(DNSResolver &&) noexcept = default;
        DNSResolver(const DNSResolver &) = delete;

        async::task<std::vector<IPv4Address>> resolve_a(const std::string &hostname);
        void set_dns_server(const IPv4Address &server) { dns_server_ = server; }

        static std::vector<u8> encode_name(const std::string &hostname);

    private:
        IPv4Address dns_server_;
        u16 next_tid_ = 1;
        std::unique_ptr<UDPSocket> sock_;

        Result<void> ensure_socket();
        std::vector<u8> build_query(const std::string &hostname, u16 type, u16 id);
        // min_ttl_out receives the smallest A-record TTL in the answer (if any).
        Result<std::vector<IPv4Address>> parse_response(const u8 *data,
                                                        u32 len,
                                                        u16 expected_id,
                                                        u32 *min_ttl_out = nullptr);
    };

} // namespace browser::net
