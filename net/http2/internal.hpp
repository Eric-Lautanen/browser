#pragma once
#include "../../tests/utility.hpp"

#include <string>
#include <vector>

namespace browser::net::http2 {

    inline u32 read_u32_be(const u8 *data, u32 len, u32 &pos) {
        if (pos + 4 > len)
            return 0;
        u32 v = (static_cast<u32>(data[pos]) << 24) | (static_cast<u32>(data[pos + 1]) << 16) |
                (static_cast<u32>(data[pos + 2]) << 8) | data[pos + 3];
        pos += 4;
        return v;
    }

    inline u32 peek_u24_be(const u8 *data, u32 len, u32 pos) {
        if (pos + 3 > len)
            return 0;
        return (static_cast<u32>(data[pos]) << 16) | (static_cast<u32>(data[pos + 1]) << 8) | data[pos + 2];
    }

    inline void write_u16_be(std::vector<u8> &out, u16 v) {
        out.push_back(static_cast<u8>((v >> 8) & 0xFF));
        out.push_back(static_cast<u8>(v & 0xFF));
    }

    inline void write_u32_be(std::vector<u8> &out, u32 v) {
        out.push_back(static_cast<u8>((v >> 24) & 0xFF));
        out.push_back(static_cast<u8>((v >> 16) & 0xFF));
        out.push_back(static_cast<u8>((v >> 8) & 0xFF));
        out.push_back(static_cast<u8>(v & 0xFF));
    }

    inline void write_u24_be(std::vector<u8> &out, u32 v) {
        out.push_back(static_cast<u8>((v >> 16) & 0xFF));
        out.push_back(static_cast<u8>((v >> 8) & 0xFF));
        out.push_back(static_cast<u8>(v & 0xFF));
    }

    inline const char *http_status_reason(u16 code) {
        switch (code) {
            case 100:
                return "Continue";
            case 101:
                return "Switching Protocols";
            case 200:
                return "OK";
            case 201:
                return "Created";
            case 204:
                return "No Content";
            case 206:
                return "Partial Content";
            case 301:
                return "Moved Permanently";
            case 302:
                return "Found";
            case 303:
                return "See Other";
            case 304:
                return "Not Modified";
            case 307:
                return "Temporary Redirect";
            case 308:
                return "Permanent Redirect";
            case 400:
                return "Bad Request";
            case 401:
                return "Unauthorized";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 405:
                return "Method Not Allowed";
            case 408:
                return "Request Timeout";
            case 413:
                return "Payload Too Large";
            case 414:
                return "URI Too Long";
            case 429:
                return "Too Many Requests";
            case 500:
                return "Internal Server Error";
            case 502:
                return "Bad Gateway";
            case 503:
                return "Service Unavailable";
            case 504:
                return "Gateway Timeout";
            default:
                return "";
        }
    }

}  // namespace browser::net::http2
