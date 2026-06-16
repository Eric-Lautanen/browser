#pragma once
#include <string>
#include <vector>
#include <functional>
#include "token.hpp"

namespace browser::html {

struct PreloadRequest {
    std::string url;
    std::string as;       // "image", "style", "script", "font", "fetch"
    bool is_async = false;
    bool is_defer = false;
    bool is_module = false;
};

class PreloadScanner {
public:
    using FetchCallback = std::function<void(const PreloadRequest&)>;

    void set_fetch_callback(FetchCallback cb);
    void scan_token(const Token& token, const std::string& base_url);

    const std::vector<PreloadRequest>& pending() const;
    void clear_pending();

private:
    FetchCallback fetch_cb_;
    std::vector<PreloadRequest> pending_;
};

} // namespace browser::html
