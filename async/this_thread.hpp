#pragma once
#include <string>
#include <windows.h>

namespace browser::async::this_thread {

inline void set_name(const std::string& name) {
    (void)name;
    SetThreadDescription(GetCurrentThread(), std::wstring(name.begin(), name.end()).c_str());
}

inline void yield() {
    SwitchToThread();
}

} // namespace browser::async::this_thread
