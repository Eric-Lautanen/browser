#pragma once
#include "../async/task.hpp"
#include "../net/http_client.hpp"

#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace browser::html {

    enum class ResourcePriority {
        CSS,  // highest - blocks render
        JS,   // depends on async/defer
        IMAGE,
        FONT,
        PREFETCH  // lowest
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
        explicit ResourceLoader(net::HTTPClient *http);
        ~ResourceLoader() = default;

        // Request a resource fetch. Returns false if duplicate URL.
        bool request(const ResourceRequest &req);

        // Fetches all pending resources (blocking - use on thread pool)
        std::vector<ResourceResponse> fetch_all();

        // BR-P5: fetches all pending resources with bounded concurrency. Each
        // in-flight request uses its own HTTPClient connection; results keep
        // request order. Blocking - use on a pool thread.
        async::task<std::vector<ResourceResponse>> fetch_all_parallel(size_t max_concurrency = 4);

        // Fetch a single resource immediately
        ResourceResponse fetch_single(const std::string &url, ResourcePriority priority = ResourcePriority::IMAGE);

        // BR-P5: one fetch on a caller-owned client; exposed for the parallel
        // worker (which passes its own HTTPClient per connection).
        static async::task<ResourceResponse> do_fetch_async(std::unique_ptr<net::HTTPClient> client,
                                                            const std::string &url);

        // Check if URL already requested
        bool is_requested(const std::string &url) const;

        // Get all pending URLs (for iteration without fetching)
        std::vector<std::string> pending_urls() const;

        // Cancel all pending
        void cancel();

    private:
        net::HTTPClient *http_;
        std::unordered_map<std::string, ResourcePriority> requested_urls_;
        std::vector<ResourceRequest> pending_;

        ResourceResponse do_fetch(const std::string &url);
    };

}  // namespace browser::html
