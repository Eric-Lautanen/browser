#include "download_manager.hpp"

#include "../async/executor.hpp"
#include "../net/http_client.hpp"
#include "../net/url.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>
#include <windows.h>

namespace browser {

    DownloadManager::DownloadManager() {
        InitializeCriticalSection(&items_mutex_);
    }

    bool DownloadManager::should_download(const std::string &url,
                                          const std::string &content_disposition,
                                          const std::string &mime_type,
                                          u64 content_length) {
        if (behavior_ == DownloadBehavior::DISABLED) {
            return false;
        }

        if (exceeds_size_cap(content_length)) {
            return false;
        }

        if (behavior_ == DownloadBehavior::NOTIFY) {
            return false;
        }

        std::string filename;
        auto cd_pos = content_disposition.find("filename=");
        if (cd_pos != std::string::npos) {
            filename = content_disposition.substr(cd_pos + 9);
            if (!filename.empty() && filename[0] == '"') {
                filename = filename.substr(1);
            }
            if (!filename.empty() && filename.back() == '"') {
                filename.pop_back();
            }
        }
        if (filename.empty()) {
            auto slash_pos = url.rfind('/');
            if (slash_pos != std::string::npos) {
                filename = url.substr(slash_pos + 1);
            }
            auto qpos = filename.find('?');
            if (qpos != std::string::npos) {
                filename = filename.substr(0, qpos);
            }
        }
        if (filename.empty()) {
            filename = "download";
        }

        // Sanitize filename: strip path separators, drive letters, and parent dir references
        {
            std::string safe;
            for (char c : filename) {
                if (c == '/' || c == '\\' || c == ':' || c == '\0') {
                    safe += '_';
                } else {
                    safe += c;
                }
            }
            // Also collapse any remaining ".." as a safety measure
            for (auto pos = safe.find(".."); pos != std::string::npos; pos = safe.find("..", pos)) {
                safe.replace(pos, 2, "__");
            }
            filename = safe;
        }

        if (is_extension_blocked(filename)) {
            return false;
        }
        if (!mime_type.empty() && is_mime_mismatch(filename, mime_type)) {
            return false;
        }

        return true;
    }

    async::task<Result<void>> DownloadManager::start_download(const std::string &url) {
        co_await async::thread_pool_executor{};

        net::HTTPClient http;
        net::http::Request req;
        auto parsed_r = net::URL::parse(url);
        if (parsed_r.is_err()) {
            co_return Result<void>(std::string("Invalid URL: ") + parsed_r.unwrap_err());
        }
        req.method = net::http::Method::GET;
        req.url = parsed_r.unwrap();
        {
            std::string host_hdr = req.url.host;
            if (req.url.port != 0 && req.url.port != req.url.default_port())
                host_hdr += ":" + std::to_string(req.url.port);
            req.headers.set("Host", host_hdr);
        }
        req.headers.set("User-Agent", "Browser/0.1");

        auto resp_r = co_await http.fetch_async(req);
        if (resp_r.is_err()) {
            co_return Result<void>(std::string("Download failed: ") + resp_r.unwrap_err());
        }
        auto resp = std::move(resp_r.unwrap());

        std::string filename;
        std::string cd = resp.headers.get("Content-Disposition");
        auto cd_pos = cd.find("filename=");
        if (cd_pos != std::string::npos) {
            filename = cd.substr(cd_pos + 9);
            if (!filename.empty() && filename[0] == '"')
                filename = filename.substr(1);
            if (!filename.empty() && filename.back() == '"')
                filename.pop_back();
        }
        if (filename.empty()) {
            auto slash_pos = url.rfind('/');
            if (slash_pos != std::string::npos)
                filename = url.substr(slash_pos + 1);
            auto qpos = filename.find('?');
            if (qpos != std::string::npos)
                filename = filename.substr(0, qpos);
        }
        if (filename.empty())
            filename = "download";

        std::string mime_type = resp.headers.get("Content-Type");

        DownloadItem item;
        item.url = url;
        item.filename = filename;
        item.mime_type = mime_type;
        item.total_bytes = static_cast<u64>(resp.body.size());
        item.received_bytes = item.total_bytes;

        // Ensure ./downloads/ directory exists
        CreateDirectoryA("./downloads", nullptr);

        std::string filepath = "./downloads/" + filename;

        {
            std::ofstream f(filepath, std::ios::binary);
            if (!f.is_open()) {
                item.error = "Cannot write file: " + filepath;
                item.cancelled = true;
                EnterCriticalSection(&items_mutex_);
                items_.push_back(std::move(item));
                LeaveCriticalSection(&items_mutex_);
                co_return Result<void>(item.error);
            }
            f.write(reinterpret_cast<const char *>(resp.body.data()), resp.body.size());
        }

        // Write Zone.Identifier ADS
        std::string zone_path = filepath + ":Zone.Identifier";
        HANDLE hZone =
            CreateFileA(zone_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hZone != INVALID_HANDLE_VALUE) {
            const char *zone_data = "[ZoneTransfer]\r\nZoneId=3\r\n";
            DWORD written = 0;
            WriteFile(hZone, zone_data, (DWORD)strlen(zone_data), &written, nullptr);
            CloseHandle(hZone);
        }

        item.completed = true;
        EnterCriticalSection(&items_mutex_);
        items_.push_back(std::move(item));
        LeaveCriticalSection(&items_mutex_);
        co_return Result<void>{};
    }

    bool DownloadManager::is_extension_blocked(const std::string &filename) {
        auto dot_pos = filename.rfind('.');
        if (dot_pos == std::string::npos)
            return false;
        std::string ext = filename.substr(dot_pos);
        for (auto &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        for (const char *const *blocked = kBlockedExtensions; *blocked; blocked++) {
            if (ext == *blocked)
                return true;
        }
        return false;
    }

    bool DownloadManager::is_mime_mismatch(const std::string &filename, const std::string &mime_type) {
        auto dot_pos = filename.rfind('.');
        if (dot_pos == std::string::npos)
            return false;
        std::string ext = filename.substr(dot_pos);
        for (auto &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (ext == ".exe" && mime_type.find("image/") == 0)
            return true;
        if (ext == ".dll" && mime_type.find("image/") == 0)
            return true;
        if (ext == ".scr" && mime_type.find("image/") == 0)
            return true;

        return false;
    }

    u32 DownloadManager::active_count() const {
        EnterCriticalSection(&items_mutex_);
        u32 count = 0;
        for (auto &item : items_) {
            if (!item.completed && !item.cancelled)
                count++;
        }
        LeaveCriticalSection(&items_mutex_);
        return count;
    }

    std::vector<DownloadItem> DownloadManager::items() const {
        EnterCriticalSection(&items_mutex_);
        auto copy = items_;
        LeaveCriticalSection(&items_mutex_);
        return copy;
    }

}  // namespace browser
