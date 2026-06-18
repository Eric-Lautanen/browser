#include "resource_loader.hpp"

#include "../net/http_client.hpp"
#include "../net/url.hpp"

#include <algorithm>

namespace browser::html {

    ResourceLoader::ResourceLoader(net::HTTPClient *http) : http_(http) {}

    bool ResourceLoader::request(const ResourceRequest &req) {
        if (req.url.empty())
            return false;
        auto it = requested_urls_.find(req.url);
        if (it != requested_urls_.end()) {
            // Already requested - update priority if higher
            if (req.priority < it->second) {
                it->second = req.priority;
            }
            return false;
        }
        requested_urls_[req.url] = req.priority;
        pending_.push_back(req);
        return true;
    }

    std::vector<ResourceResponse> ResourceLoader::fetch_all() {
        std::vector<ResourceResponse> results;

        // Sort by priority
        std::sort(pending_.begin(), pending_.end(), [](const ResourceRequest &a, const ResourceRequest &b) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        });

        for (const auto &req : pending_) {
            ResourceResponse resp = do_fetch(req.url);
            results.push_back(std::move(resp));
        }

        pending_.clear();
        return results;
    }

    ResourceResponse ResourceLoader::fetch_single(const std::string &url, ResourcePriority priority) {
        if (url.empty())
            return {url, {}, false, "Empty URL"};

        if (is_requested(url)) {
            return {url, {}, false, "Already requested"};
        }

        requested_urls_[url] = priority;
        auto resp = do_fetch(url);
        if (!resp.success) {
            requested_urls_.erase(url);
        }
        return resp;
    }

    ResourceResponse ResourceLoader::do_fetch(const std::string &url_str) {
        ResourceResponse resp;
        resp.url = url_str;

        auto parsed = net::URL::parse(url_str);
        if (parsed.is_err()) {
            resp.error_msg = "Invalid URL: " + parsed.unwrap_err();
            return resp;
        }

        net::http::Request req;
        req.method = net::http::Method::GET;
        req.url = parsed.unwrap();

        {
            std::string host_hdr = req.url.host;
            if (req.url.port != 0 && req.url.port != req.url.default_port())
                host_hdr += ":" + std::to_string(req.url.port);
            req.headers.set("Host", host_hdr);
        }
        req.headers.set("User-Agent", "Browser/0.1");
        req.headers.set("Accept", "*/*");
        req.headers.set("Accept-Encoding", "gzip, deflate");

        auto fetch_r = http_->fetch_async(req).sync_wait();
        if (fetch_r.is_err()) {
            resp.error_msg = fetch_r.unwrap_err();
            return resp;
        }

        auto http_resp = std::move(fetch_r.unwrap());
        resp.data = std::move(http_resp.body);
        resp.success = true;
        return resp;
    }

    bool ResourceLoader::is_requested(const std::string &url) const {
        return requested_urls_.find(url) != requested_urls_.end();
    }

    std::vector<std::string> ResourceLoader::pending_urls() const {
        std::vector<std::string> urls;
        urls.reserve(pending_.size());
        for (const auto &req : pending_) {
            urls.push_back(req.url);
        }
        return urls;
    }

    void ResourceLoader::cancel() {
        pending_.clear();
        requested_urls_.clear();
    }

}  // namespace browser::html
