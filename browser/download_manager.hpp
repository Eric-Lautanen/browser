#pragma once
#include "../async/task.hpp"
#include "types.hpp"

#include <string>
#include <vector>
#include <windows.h>

namespace browser {

struct DownloadItem {
    std::string url;
    std::string filename;
    std::string mime_type;
    u64 total_bytes = 0;
    u64 received_bytes = 0;
    bool completed = false;
    bool cancelled = false;
    std::string error;
};

enum class DownloadBehavior { DISABLED, NOTIFY, ENABLED };

class DownloadManager {
public:
    DownloadManager();

    bool should_download(const std::string& url, const std::string& content_disposition,
                         const std::string& mime_type, u64 content_length);

    async::task<Result<void>> start_download(const std::string& url);

    static bool is_extension_blocked(const std::string& filename);
    static bool is_mime_mismatch(const std::string& filename, const std::string& mime_type);

    static bool exceeds_size_cap(u64 bytes) { return bytes > 2ULL * 1024 * 1024 * 1024; }

    DownloadBehavior behavior() const { return behavior_; }
    void set_behavior(DownloadBehavior b) { behavior_ = b; }

    std::vector<DownloadItem> items() const;
    u32 active_count() const;

private:
    mutable CRITICAL_SECTION items_mutex_;
    std::vector<DownloadItem> items_;
    DownloadBehavior behavior_ = DownloadBehavior::DISABLED;

    static constexpr const char* kBlockedExtensions[] = {
        ".exe", ".msi", ".scr", ".vbs", ".ps1", ".jar", ".bat", ".cmd",
        ".dll", ".ocx", ".msu", ".msp", ".reg", ".pif", ".cpl",
        ".app", ".gadget", ".hta", ".wsf", ".wsh", nullptr
    };
};

}  // namespace browser
