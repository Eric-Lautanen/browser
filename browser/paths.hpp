#pragma once
#include <string>
#include <cstdlib>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace browser {

inline std::string data_dir() {
    static std::string dir;
    if (!dir.empty())
        return dir;
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("USERPROFILE", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        dir = std::string(buf) + "\\Documents\\browser";
        CreateDirectoryA(dir.c_str(), nullptr);
    } else {
        dir = "browser";
    }
    return dir;
}

}  // namespace browser
