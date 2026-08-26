// clang-format off
#include <winsock2.h>
// clang-format on
#include "resource_loader.hpp"

#include "../async/executor.hpp"
#include "../net/http_client.hpp"
#include "../net/url.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <windows.h>

namespace browser::html {

    namespace {

        // BR-P5: shared state for the bounded-concurrency fetch join.
        struct ParallelFetchState {
            const std::vector<ResourceRequest> *pending = nullptr;
            std::vector<ResourceResponse> *results = nullptr;
            std::atomic<size_t> next{0};
            std::atomic<size_t> remaining{0};
            HANDLE done = nullptr;
        };

        async::task<void> parallel_fetch_worker(ParallelFetchState *st) {
            for (;;) {
                size_t i = st->next.fetch_add(1, std::memory_order_relaxed);
                if (i >= st->pending->size())
                    break;
                // Each worker owns its HTTPClient so concurrent requests get
                // independent connections (DNS cache + keep-alive still cut
                // the per-connection setup cost).
                auto client = std::make_unique<net::HTTPClient>();
                auto r = co_await ResourceLoader::do_fetch_async(std::move(client), (*st->pending)[i].url);
                (*st->results)[i] = r.is_ok() ? std::move(r.unwrap()) : ResourceResponse{};
            }
            if (st->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                SetEvent(st->done);
            }
        }

    }  // namespace

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

    async::task<std::vector<ResourceResponse>> ResourceLoader::fetch_all_parallel(size_t max_concurrency) {
        co_await async::thread_pool_executor{};

        std::sort(pending_.begin(), pending_.end(), [](const ResourceRequest &a, const ResourceRequest &b) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        });

        std::vector<ResourceResponse> results(pending_.size());

        if (!pending_.empty()) {
            ParallelFetchState st;
            st.pending = &pending_;
            st.results = &results;

            size_t workers = max_concurrency < pending_.size() ? max_concurrency : pending_.size();
            // The join counts WORKERS: each worker decrements once when its
            // item loop is exhausted, and the last one signals the event.
            st.remaining.store(workers, std::memory_order_relaxed);
            st.done = CreateEvent(nullptr, TRUE, FALSE, nullptr);

            // Keep the coroutine frames alive until every worker finished;
            // destroying a running task destroys its frame (BR-N2 class).
            std::vector<std::unique_ptr<async::task<void>>> tasks;
            tasks.reserve(workers);
            for (size_t i = 0; i < workers; i++) {
                auto t = std::make_unique<async::task<void>>(parallel_fetch_worker(&st));
                t->start();
                tasks.push_back(std::move(t));
            }

            WaitForSingleObject(st.done, 60000);
            CloseHandle(st.done);
            // All worker coroutines have completed by now; safe to destroy.
        }

        pending_.clear();
        co_return results;
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
        auto client = std::make_unique<net::HTTPClient>();
        auto task = do_fetch_async(std::move(client), url_str);
        auto r = task.sync_wait();
        return r.is_ok() ? std::move(r.unwrap()) : ResourceResponse{};
    }

    async::task<ResourceResponse> ResourceLoader::do_fetch_async(std::unique_ptr<net::HTTPClient> client,
                                                                 const std::string &url_str) {
        ResourceResponse resp;
        resp.url = url_str;

        auto parsed = net::URL::parse(url_str);
        if (parsed.is_err()) {
            resp.error_msg = "Invalid URL: " + parsed.unwrap_err();
            co_return resp;
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

        auto fetch_r = co_await client->fetch_async(req);
        if (fetch_r.is_err()) {
            resp.error_msg = fetch_r.unwrap_err();
            co_return resp;
        }

        auto http_resp = std::move(fetch_r.unwrap());
        resp.data = std::move(http_resp.body);
        resp.success = true;
        co_return resp;
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
