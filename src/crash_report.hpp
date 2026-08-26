#pragma once
// Temporary diagnostics aid (session 2): SEH filter that must survive STACK
// OVERFLOW (0xC00000FD). Uses only Win32 file APIs with fixed buffers — no
// CRT calls, no heap allocations — so it can execute with almost no stack.
#include <windows.h>
#include <psapi.h>

namespace browser {

    inline constexpr int kMaxBreadcrumbs = 64;
    inline constexpr int k_bc_msg_len = 96;
    inline char g_bc_slots[kMaxBreadcrumbs][k_bc_msg_len];
    inline volatile long g_bc_head = 0;
    inline volatile const char *g_crash_tag = "browser";
    // Temporary: first 512 window messages logged from wnd_proc.
    inline unsigned int g_wnd_msg_log[512] = {};
    inline volatile long g_wnd_msg_n = 0;

    inline void bc(const char *msg) {
        OutputDebugStringA("BC: ");
        OutputDebugStringA(msg);
        OutputDebugStringA("\n");
        long slot = InterlockedExchangeAdd(&g_bc_head, 1) % kMaxBreadcrumbs;
        char *dst = g_bc_slots[slot];
        int i = 0;
        while (msg[i] && i < k_bc_msg_len - 1) {
            dst[i] = msg[i];
            i++;
        }
        dst[i] = 0;
    }

    // Minimal hex/pointer writers (no CRT).
    inline void cr_append(char *buf, int &n, const char *s) {
        while (*s && n < 512)
            buf[n++] = *s++;
    }
    inline void cr_append_hex(char *buf, int &n, unsigned long long v) {
        static const char *d = "0123456789ABCDEF";
        char tmp[32];
        int t = 0;
        if (!v)
            tmp[t++] = '0';
        while (v && t < 32)
            tmp[t++] = d[v & 0xF], v >>= 4;
        while (t--)
            buf[n++] = tmp[t];
    }

    inline LONG WINAPI crash_filter(EXCEPTION_POINTERS *info) {
        DWORD code = info ? info->ExceptionRecord->ExceptionCode : 0;
        void *addr = info && info->ExceptionRecord->ExceptionAddress ? info->ExceptionRecord->ExceptionAddress : nullptr;

        char buf[1024];
        int n = 0;
        cr_append(buf, n, "CRASH code=0x");
        cr_append_hex(buf, n, static_cast<unsigned long>(code));
        cr_append(buf, n, " addr=0x");
        cr_append_hex(buf, n, reinterpret_cast<unsigned long long>(addr));
        cr_append(buf, n, "\r\n");
        long head = g_bc_head;
        long start = head > kMaxBreadcrumbs ? head - kMaxBreadcrumbs : 0;
        for (long i = start; i < head; i++) {
            cr_append(buf, n, "bc: ");
            cr_append(buf, n, g_bc_slots[i % kMaxBreadcrumbs]);
            cr_append(buf, n, "\r\n");
            if (n > 900)
                break;
        }

        cr_append(buf, n, "msgs: ");
        long total = g_wnd_msg_n;
        if (total > 512)
            total = 512;
        for (long i = 0; i < total && n < 900; i++) {
            cr_append_hex(buf, n, static_cast<unsigned long long>(g_wnd_msg_log[i]));
            if (i + 1 < total)
                cr_append(buf, n, ",");
        }
        cr_append(buf, n, "\r\n");

        // Heuristic stack trace: scan the faulting thread's stack for values
        // inside the main module (likely return addresses).
        if (info && info->ContextRecord && info->ExceptionRecord->ExceptionAddress) {
            HMODULE mod = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(crash_filter), &mod);
            uintptr_t lo = reinterpret_cast<uintptr_t>(mod);
            uintptr_t hi = lo;
            if (mod) {
                MODULEINFO mi;
                if (GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi)))
                    hi = lo + static_cast<uintptr_t>(mi.SizeOfImage);
                else
                    hi = lo + 0x10000000ull;
            }
            CONTEXT *ctx = info->ContextRecord;
            uintptr_t rsp = ctx->Rsp;
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(reinterpret_cast<LPCVOID>(rsp), &mbi, sizeof(mbi)) &&
                mbi.State == MEM_COMMIT) {
                uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
                uintptr_t limit = rsp + 0x4000 < region_end ? rsp + 0x4000 : region_end;
                int found = 0;
                for (uintptr_t p = rsp; p + 8 <= limit && found < 24; p += 8) {
                    uintptr_t v = *reinterpret_cast<uintptr_t *>(p);
                    if (v >= lo && v < hi) {
                        cr_append(buf, n, "ret 0x");
                        cr_append_hex(buf, n, v - lo);
                        cr_append(buf, n, "\r\n");
                        found++;
                    }
                }
            } else {
                cr_append(buf, n, "stack unreadable\r\n");
            }
        }

        HANDLE hfile = CreateFileA("C:/github/browser/build/crash_report.txt", GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hfile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hfile, buf, static_cast<DWORD>(n), &written, nullptr);
            CloseHandle(hfile);
        }
        MessageBoxA(nullptr, buf, "browser crashed", MB_OK | MB_ICONERROR);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    inline void install_crash_reporter(const char *tag) {
        g_crash_tag = tag;
        SetUnhandledExceptionFilter(crash_filter);
    }

} // namespace browser
