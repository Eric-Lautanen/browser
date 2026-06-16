#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <queue>
#include <memory>
#include "../async/task.hpp"
#include "../net/http_client.hpp"

namespace browser::html {

enum class ResourcePriority {
    CSS,       // highest - blocks render
    JS,        // depends on async/defer
    IMAGE,
    FONT,
    PREFETCH   // lowest
};

struct ResourceRequest {
    std::string url;
    ResourcePriority priority = ResourcePriority::IMAGE;
    bool is_async = false;
    bool is_defer = false;
    bool is_module = false;
};

struct ResourceResponse {
    std::string url;
    std::vector<u8> data;
    bool success = false;
    std::string error_msg;
};

class ResourceLoader {
public:
    explicit ResourceLoader(net::HTTPClient* http);
    ~ResourceLoader() = default;

    // Request a resource fetch. Returns false if duplicate URL.
    bool request(const ResourceRequest& req);

    // Fetches all pending resources (blocking - use on thread pool)
    std::vector<ResourceResponse> fetch_all();

    // Fetch a single resource immediately
    ResourceResponse fetch_single(const std::string& url, ResourcePriority priority = ResourcePriority::IMAGE);

    // Check if URL already requested
    bool is_requested(const std::string& url) const;

    // Get all pending URLs (for iteration without fetching)
    std::vector<std::string> pending_urls() const;

    // Cancel all pending
    void cancel();

private:
    net::HTTPClient* http_;
    std::unordered_map<std::string, ResourcePriority> requested_urls_;
    std::vector<ResourceRequest> pending_;

    ResourceResponse do_fetch(const std::string& url);
};

} // namespace browser::html
