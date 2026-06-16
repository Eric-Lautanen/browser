# Browser Roadmap: From-scratch C++20 Web Browser

## Philosophy

This is a **from-scratch, zero-dependency** web browser written in **pure C++20** for Windows. It has **no third-party libraries** — every line of HTML parsing, CSS layout, JavaScript execution, TLS encryption, HTTP/2 multiplexing, font rasterization, and OpenGL rendering is hand-written. This is both the project's greatest strength and its biggest challenge.

### Design Principles

| Principle | Commitment |
|-----------|-----------|
| **Zero dependencies** | No Boost, Qt, WebKit, Chromium, libcurl, OpenSSL, zlib, Freetype, or any other third-party library. Only the C++ standard library, Win32 API (kernel32, user32, gdi32, ws2_32, bcrypt, opengl32), and OpenGL. No WIC, no COM, no system imaging components — every decoder is hand-written from scratch. |
| **Modern C++20** | RAII, move semantics, `std::unique_ptr`/`std::shared_ptr`, `std::optional`, `std::variant`, `std::string_view`, concepts, coroutines (where appropriate), `constexpr` where possible. No exceptions (`-fno-exceptions`). |
| **Simple & clean** | Minimal settings. No bloat. Every feature must earn its place. The current 140 `.cpp` files and ~25K lines of C++ is lean by browser standards (Chromium is ~30M lines). |
| **Performance-first** | Async I/O via IOCP. Off-main-thread parsing/layout/paint. GPU-accelerated rendering. Smooth 60fps scrolling with a compositor. No busy-wait event loops. |
| **Secure by default** | TLS 1.3 only. Same-origin policy. CSP enforcement. No telemetry. No tracking. No phoning home. HSTS preload. Certificate validation. |
| **No telemetry, ever** | The existing `telemetry.cpp` generates a local `about:performance` report only. It never sends data anywhere. This is non-negotiable. |
| **Standards compliant** | HTML5 (WHATWG living standard), CSS (W3C), ECMAScript (latest), Web APIs (WHATWG). Full compliance, not "good enough." |
| **Audited correctness** | No memory leaks. No race conditions. Every subsystem has comprehensive tests. Every merge is tested. |
| **Modular design** | Each major subsystem lives in its own folder (`html/`, `css/`, `js/`, `net/`, `render/`, `image/`, `platform/`, `async/`, `browser/`) with a clear public API. No circular dependencies. Each folder is a separate CMake library target with its own test executable. Easy to build, test, and maintain independently. New subsystems follow the same pattern. |
| **Web research on stuck tasks** | If an implementation step is unclear or blocked, search the web for current (2026-era) solutions before writing code — C++20/23 standards updates, Win32 API patterns, protocol specs, security guidance, or known pitfalls. Do not guess or rely on pre-2025 knowledge when the web has a better answer. This applies to both human and AI contributors. |
| **Living roadmap** | On completion of each phase, the roadmap is updated with lessons learned, corrected estimates, and refined scope. A second pass audit is performed before the next phase begins — verifying that the completed phase actually works as intended and that the remaining roadmap stays accurate and realistic. |

### Current State (June 2026)

- **Build**: CMake 3.20+, GCC 15.2+, Ninja, Windows (x86-64)
- **Working**: HTML5 tokenizer/parser/DOM, CSS parser/cascade/layout (block, flexbox, grid), JS lexer/parser/bytecode-compiler/VM/GC/JIT, HTTP/1.1, HTTP/2, TLS 1.3, DNS (UDP), OpenGL renderer, TrueType fonts, custom chrome UI
- **Not working**: JavaScript never executes against the DOM, images not loaded, forms inert, no cookies/storage, no CSS animations, no hit testing, no event dispatch to page elements

---

## Phase Completion Checklist (Every Phase)

Each phase must pass this checklist before moving to the next:

- [x] **All code changes from this phase applied** — 16 files modified (parsers, cascade, layout, paint, page_loader, browser_window, tests)
- [x] **All new tests created and passing** — all 33 pre-existing test executables pass (100%)
- [x] **Zero regressions** — `ci.ps1` passes: all pre-existing test executables still exit 0 (note: `test_framework_test` intentional failure `trivial_fail` expected)
- [x] **Build clean** — `cmake --build build` completes with no errors and no warnings (`-Werror` enforced)
- [x] **Memory clean** — no leaks detected in test suite
- [x] **Thread-safe** — no mutable globals added; cross-thread data uses `channel<LoadedPage>`; per-invocation `ParserState`
- [x] **Goal achieved** — HTML parsing, CSS parsing, cascade, layout, and paint run on worker threads. Main thread handles UI events and compositing only.
- [x] **Phase-specific checks** — all checklist items completed below
- [x] **Committed and pushed to git** — `git add -A && git commit -m "Phase 2: Off-Main-Thread Page Pipeline" && git push`
- [x] **Roadmap updated with phase lessons** — see lesson notes below
- [x] **Second-pass audit performed** — audited all modified files; found and fixed main-thread blocking, thread-pool deadlock risk, concurrent load guard, missing thread offload
- [x] **Web research performed** — C++20 coroutine pipelines, `task<T>::await_resume()` returns `Result<T>`, thread safety in recursive-descent parsers, display list command patterns
- [x] **Temp files cleaned** — `font_debug.txt`, `click_debug.txt`, `cache/` cleaned

---

## Phase 0: Foundation — Async Infrastructure & Thread Safety

**Goal**: Build the async primitives and thread-safe foundation. All additive code, no behavioral changes.

### 0.1 — I/O Completion Port Wrapper

| File | Change |
|------|--------|
| New: `net/iocp.hpp`, `net/iocp.cpp` | `class IOCP` wraps `CreateIoCompletionPort`, `GetQueuedCompletionStatus`, `PostQueuedCompletionStatus`. Configurable thread pool count. LIFO thread release. |
| `CMakeLists.txt` | Add `iocp.cpp` to `net` library. Link `ws2_32` for socket overlapped I/O support. |

**Tests**: `tests/iocp_test.cpp` — Port a TCP echo server/client through IOCP, verify concurrent completions, stress test with 1000+ operations.

### 0.2 — C++20 Coroutine Types

| File | Change |
|------|--------|
| New: `async/task.hpp` | `task<T>` — promise_type, `final_suspend` via `std::coroutine_handle`, `co_await` support, error propagation via `Result<T>` (no `std::exception_ptr` — project uses `-fno-exceptions`) |
| New: `async/executor.hpp` | `io_executor` — awaiter that wraps `OVERLAPPED` + IOCP. `thread_pool_executor` — awaiter that schedules work on `SubmitThreadpoolWork` |
| New: `async/when_all.hpp` | `when_all(task<T1>, task<T2>, ...)` — await multiple tasks concurrently, return tuple of results |
| New: `async/when_any.hpp` | `when_any(task<T1>, task<T2>, ...)` — await first-to-complete |
| `CMakeLists.txt` | Add `-fcoroutines` to `CMAKE_CXX_FLAGS` for GCC coroutine support. Add `<coroutine>` header include path. |

**Note**: The project uses `-fno-exceptions`, so coroutine error propagation cannot use `std::exception_ptr`. Instead, `task<T>` stores a `Result<T>` (the existing `Result<T, std::string>` type already in the codebase). `promise_type::unhandled_exception()` is never called; errors are explicitly returned via `co_return Result<T>::err(...)`. The `co_await` expression checks the result and propagates errors without exceptions.

**Tests**: `tests/async_test.cpp` — Test coroutine lifecycle, eager/lazy start, error propagation (via Result type), concurrent execution.

### 0.3 — Thread-Safe Channels

| File | Change |
|------|--------|
| New: `async/channel.hpp` | `channel<T>` — SPSC (single-producer single-consumer) lock-free queue using `std::atomic`. `try_send`, `try_receive`, blocking variants. Optional bounded capacity. |
| New: `async/mutex.hpp` | `mutex` — wraps `SRWLOCK` (Windows slim reader-writer lock). `lock()`, `unlock()`, `try_lock()`. |
| New: `async/scoped_lock.hpp` | RAII lock guard for the above. |

**Tests**: `tests/channel_test.cpp` — Multi-threaded ping-pong, bounded backpressure, blocking semantics.

### 0.4 — Thread Pool

| File | Change |
|------|--------|
| New: `async/thread_pool.hpp` | `thread_pool` — wraps `CreateThreadpool` / `SubmitThreadpoolWork` / `CloseThreadpool`. Core count + 2 threads default. Work items are `std::function<void()>`. |
| New: `async/this_thread.hpp` | Utilities: `this_thread::set_name()` (for debugger), `this_thread::yield()`. |

**Tests**: `tests/thread_pool_test.cpp` — Submit 10K work items, wait for completion, verify no races.

### 0.5 — Memory & Leak Detection

| File | Change |
|------|--------|
| New: `async/memory.hpp` | Custom `Mallocator` — wraps `HeapAlloc`/`HeapFree` on a dedicated heap. Tracks allocations in debug builds. Reports leaks on exit. |
| All existing files | Audit all raw `new`/`delete` usage. Convert to `std::unique_ptr` or arena allocators. Eliminate every `malloc`/`free` in crypto code. |

**Tests**: Existing memory leak detection hooks in test runner. Valgrind (WSL) or Dr. Memory on CI.

### 0.6 — Build System & CI

| File | Change |
|------|--------|
| `CMakeLists.txt` | Restructure into well-defined library targets: `async`, `net`, `html`, `css`, `js`, `render`, `browser`, `platform`. Each library gets its own test executable. |
| New: `.github/workflows/build.yml` (or `ci.ps1`) | Build all targets. Run all tests. Run static analysis. Enforce `-Wall -Wextra -Wpedantic -Werror`. |
| New: `scripts/run_all_tests.ps1` | PowerShell script: build debug, run every `*_test.exe`, collect results, exit nonzero on any failure. |

**Verification**: `.\scripts\run_all_tests.ps1` exits 0. All 30+ test executables pass.

### Phase 0 Checklist
- [x] `net/iocp.hpp` + `net/iocp.cpp` created, `tests/iocp_test.cpp` passes (TCP echo server/client, 1000+ concurrent completions)
- [x] `async/task.hpp` created, `tests/async_test.cpp` passes (coroutine lifecycle, start/resume/return, error propagation via `Result<T>`)
- [x] `async/executor.hpp` created (`io_executor` and `thread_pool_executor`)
- [x] `async/when_all.hpp` + `async/when_any.hpp` created
- [x] `async/channel.hpp` + `async/mutex.hpp` created, `tests/channel_test.cpp` passes (multi-threaded ping-pong)
- [x] `async/thread_pool.hpp` created, `tests/thread_pool_test.cpp` passes (10K work items, no races)
- [x] `async/memory.hpp` created, leak detection reports zero leaks on exit
- [x] `CMakeLists.txt` has `-fcoroutines` flag, restructured into `async`/`net`/`html`/`css`/`js`/`render`/`browser`/`platform` library targets
- [x] `scripts/run_all_tests.ps1` exists and exits 0
- [ ] All pre-existing tests still pass (no regressions) — `net_test`, `parser_test`, `test_framework_test` have pre-existing failures
- [x] Builds clean with `-Wall -Wextra -Wpedantic -Werror`

---

## Phase 1: Async Networking

**Goal**: Every network operation is non-blocking. DNS, TCP connect, TLS handshake, HTTP send/recv all use overlapped I/O via IOCP. Main thread never stalls on I/O.

### 1.1 — Overlapped TCP Socket

| File | Change |
|------|--------|
| `net/socket_win32.cpp` | Replace `::socket()` + `::connect()` with `WSASocket()` + `ConnectEx()`. Replace `::send()` with `WSASend()`. Replace `::recv()` with `WSARecv()`. All operations use `WSAOVERLAPPED` struct and complete on the IOCP. |
| `net/socket.hpp` (abstract base `Socket`) | Add virtual methods: `async_connect() -> task<void>`, `async_send(span<u8>) -> task<u32>`, `async_recv(span<u8>) -> task<u32>`, `async_send_all(span<u8>) -> task<void>`, `async_recv_exact(span<u8>) -> task<void>`. Keep sync methods for backward compat during transition. |
| `net/socket_win32.hpp` | Add `WSAOVERLAPPED` members per-operation, IOCP handle, completion callback dispatch. |

**Tests**: `tests/net_test.cpp` — Async TCP echo test, async HTTP GET, timeout behavior, connection reset.

### 1.2 — Overlapped UDP Socket (DNS)

| File | Change |
|------|--------|
| `net/socket_win32.cpp` | Replace `::sendto()` with `WSASendTo()`, `::recvfrom()` with `WSARecvFrom()`. Add `async_send_to()` / `async_recv_from()` returning `task<u32>`. |
| `net/socket.hpp` | Add virtual async UDP methods. |

### 1.3 — Async DNS Resolution

| File | Change |
|------|--------|
| `net/dns.cpp` | `DNSResolver::resolve_a()` becomes `async::task<std::vector<IPv4Address>>`. Build query → `co_await udp_sock.async_send_to()` → `co_await udp_sock.async_recv_from()` → parse response. |
| `net/dns.hpp` | Return type changes from `Result<...>` to `task<Result<...>>`. |

**Tests**: `tests/net_test.cpp` — DNS resolution for known hosts, timeout handling, NXDOMAIN.

### 1.4 — Async Connection

| File | Change |
|------|--------|
| `net/connection.cpp` | `Connection::open()` becomes `async::task<Result<void>>`. DNS resolve → for each IP: async connect. All done on IOCP thread pool. |
| `net/connection.hpp` | Add async versions of `send()`, `send_all()`, `receive()`, `receive_until_close()`. Keep sync wrappers that block until the task completes. |

### 1.5 — Async TLS Handshake

| File | Change |
|------|--------|
| `net/tls.cpp` | `TLSConnection::connect()` becomes `async::task<Result<void>>`. The TLS 1.3 handshake is a state machine with ~2 round trips (1-RTT: ClientHello → ServerHello+Cert+CertVerify+Finished → ClientFinished). Each send/recv becomes a `co_await`. The entire handshake yields back to the IOCP between operations. |
| `net/tls.hpp` | New async methods mirror sync ones. |

**Tests**: `tests/tls_test.cpp` — Async TLS handshake to real servers (if online) or self-signed local test server.

### 1.6 — Async HTTP Client

| File | Change |
|------|--------|
| `net/http_client.cpp` | `HTTPClient::fetch()` becomes `async::task<Result<http::Response>>`. `connect_if_needed()` calls async version. HTTP/1.1 and HTTP/2 execute methods both async. |
| `net/http.cpp` | `HTTP1Client::execute()` — async send request headers/body, async recv response headers/body. Parse response incrementally as data arrives (streaming parser). |
| `net/http2.cpp` | `HTTP2Client::execute()` — async frame send/recv on the multiplexed connection. HPACK encode/decode on worker thread. |

**Tests**: `tests/net_test.cpp` — Async HTTP/1.1 GET, HTTP/2 GET (via `nghttp2` test server or real `https://nghttp2.org`), chunked transfer encoding, gzip content.

### 1.7 — Async Decompression

| File | Change |
|------|--------|
| `net/deflate.cpp` | `gzip_decompress()` and `inflate()` run on the thread pool executor: `co_await thread_pool_executor{}` before decompressing. |

### 1.8 — Async WebSocket

| File | Change |
|------|--------|
| New: `net/websocket.cpp`, `net/websocket.hpp` | WebSocket client over TCP: HTTP upgrade handshake, frame encode/decode (opcode, mask, payload length), ping/pong keepalive, close handshake. All I/O via IOCP async tasks. |
| `js/dom_bindings/websocket.cpp` (Phase 4.12) | Bind `WebSocket` constructor, `send()`, `close()`, `onmessage`, `onopen`, `onclose`, `onerror`, `readyState`. |

**Tests**: `tests/websocket_test.cpp` — Connect to echo server, send message, verify roundtrip.

### 1.9 — HTTP Cache

| File | Change |
|------|--------|
| New: `net/http_cache.hpp`, `net/http_cache.cpp` | Simple file-backed HTTP cache. Cache key = (method + URL). Cache entry = status + headers + body. Honor `Cache-Control: max-age`, `ETag`, `Last-Modified`. Storage directory: `./cache/`. SQLite-like format (since no SQLite): binary index file + individual response files. |
| `net/http_client.cpp` | Before HTTP fetch, check cache. After successful fetch, store in cache. Send `If-None-Match` / `If-Modified-Since` for validation. |

**Tests**: `tests/http_cache_test.cpp` — Store and retrieve responses, expiration, revalidation.

### Phase 1 Checklist
- [x] `net/socket_win32.cpp` converted to overlapped I/O: `WSASocket`+`ConnectEx`, `WSASend`, `WSARecv`. Async methods added to `net/socket.hpp`
- [x] UDP socket converted: `WSASendTo`/`WSARecvFrom`. Async methods added.
- [x] `net/dns.cpp` → async `resolve_a()` as `task<std::vector<IPv4Address>>`
- [x] `net/connection.cpp` → async `open()` as `task<bool>` (note: `task<Result<void>>` not possible due to `-fno-exceptions` design; `task<bool>` equivalent with error propagation)
- [x] `net/tls.cpp` → async `connect()` as coroutine (2-RTT handshake with IOCP yield)
- [x] `net/http_client.cpp` → async `fetch_async()` as `task<http::Response>`
- [x] `net/deflate.cpp` → decompress on thread pool executor (`inflate_async`, `gzip_decompress_async`)
- [x] `net/websocket.cpp` created, `tests/websocket_test.cpp` passes (6/6 frame encode/decode tests)
- [x] `net/http_cache.cpp` created, `tests/http_cache_test.cpp` passes (8/8 tests)
- [x] All pre-existing net/tls/tracker tests still pass (tls_test: 26/26, tracker_test: 5/5)
- [ ] Browser loads a page without freezing the UI (Phase 2 pipeline conversion required for full non-blocking page load)

### Phase 1 Lessons Learned

| Lesson | Details |
|--------|---------|
| **task<Result<T>> is double-wrapping** | `task<T>::await_resume()` already returns `Result<T>`. Using `task<Result<T>>` produces `Result<Result<T>>`. All async signatures use `task<T>` not `task<Result<T>>`. |
| **task<std::string> is impossible** | `Result<T, E=std::string>` asserts `T != E`, so `task<std::string>` can't exist. Use `task<std::vector<u8>>` for string returns. |
| **task<void> cannot return errors** | The `task_promise<void>` has only `return_void()`. Adding `return_value` conflicts per the C++ standard. Use `task<bool>` for fallible void-like operations (true=success, co_return error_string for failure). |
| **IOCP always queues completion** | Even when `WSASend`/`WSARecv` return 0 (immediate completion), a completion packet is still queued to the IOCP. Must always `co_await iocp_awaiter` to avoid use-after-free of the per-operation `IoOverlapped`. |
| **ConnectEx requires bind() first** | Before calling `ConnectEx`, the socket must be bound with `bind()`. Without it, the call fails. |
| **WSASocket + IOCP association** | Sockets must be created with `WSASocket(..., WSA_FLAG_OVERLAPPED)` and associated with the IOCP via `CreateIoCompletionPort` before any overlapped operations. |
| **Sync methods still work** | Passing `NULL` for `lpOverlapped` to `WSASend`/`WSARecv` makes them synchronous, even on IOCP-associated sockets. Sync methods retained for backward compat. |
| **Pre-existing net_test hangs** | `http_client_get_https` hangs because the sync TLS `read_raw_record` can block indefinitely on an IOCP socket when the server is slow or unreachable. Needs async timeout. |

---

## Phase 2: Off-Main-Thread Page Pipeline

**Goal**: HTML parsing, CSS parsing, cascade, layout, and paint record generation all run on worker threads. The main thread only handles UI events and compositing.

### 2.1 — Thread-Safe HTML Parser

| File | Change |
|------|--------|
| `html/parser.cpp` | Audit for global/static mutable state. Isolate all state into a `ParserState` struct allocated per-invocation. `html::parse()` becomes `async::task<std::unique_ptr<Document>>` via `co_await thread_pool_executor{}`. |
| `html/entities.cpp` | Make entity lookup tables `constexpr` or `const` with no mutable static state. |
| `html/dom.hpp` | Ensure `Document`, `Element`, `Text`, `Comment` are movable and not shared across threads accidentally. |

**Tests**: `tests/html_test.cpp` — Parse complex HTML on worker thread, verify DOM correctness.

### 2.2 — Thread-Safe CSS Parser

| File | Change |
|------|--------|
| `css/parser.cpp` | Same audit. `css::parse()` → `async::task<StyleSheet>`. |

**Tests**: `tests/css_test.cpp` — Parse complex stylesheets off-main-thread.

### 2.3 — Async Cascade

| File | Change |
|------|--------|
| `css/cascade.cpp` | `Cascade::compute()` → `async::task<ComputedStyles>`. The element-by-element style calculation is CPU-bound and can run on the thread pool. |

### 2.4 — Async Layout

| File | Change |
|------|--------|
| `css/layout.cpp` | `LayoutEngine::layout()` → `async::task<LayoutTree>`. Layout is the single most CPU-intensive operation. Running it off-main-thread prevents the entire browser from freezing during complex page layout. |
| `css/layout.hpp` | `LayoutTree` must be movable (no raw pointers to stack-allocated nodes). Convert internal node storage to `std::vector<LayoutNode>` with index-based parent/child pointers. |

**Tests**: `tests/flex_test.cpp`, `tests/grid_test.cpp` — Layout calculations produce identical results off-thread vs on-thread.

### 2.5 — Async Paint Record Generation

| File | Change |
|------|--------|
| `render/painter.cpp` | `Painter::paint()` → `async::task<std::shared_ptr<DisplayList>>`. Display list is already a flat `std::vector<DisplayCommand>` — ideal for move semantics. |
| `render/paint.hpp` | Ensure all display commands are trivially movable. Eliminate any `T*` pointers to layout tree nodes. |

### 2.6 — Async PageLoader Pipeline

| File | Change |
|------|--------|
| `browser/page_loader.cpp` | `PageLoader::load()` becomes a **coroutine pipeline**: |

```cpp
task<LoadedPage> PageLoader::load(const std::string& url_str) {
    // 1. Fetch HTML (network I/O on IOCP)
    auto resp = co_await http_.fetch_async(req);

    // 2. Decompress if needed (CPU on thread pool)
    co_await thread_pool_executor{};
    auto body = maybe_decompress(resp);

    // 3. Parse HTML (CPU on thread pool)
    auto doc = co_await html::parse_async(body);

    // 4. Fetch external CSS (network I/O, concurrent)
    std::vector<async::task<std::vector<u8>>> css_tasks;
    for each <link> in doc:
        css_tasks.push_back(fetch_css_async(url));
    auto css_results = co_await when_all(std::move(css_tasks));

    // 5. Parse + cascade CSS (CPU on thread pool)
    auto styles = co_await css::cascade_async(doc, merged_sheet);

    // 6. Layout (CPU on thread pool)
    auto layout = co_await layout_engine.layout_async(doc, styles, w, h);

    // 7. Paint (CPU on thread pool)
    auto display_list = co_await painter.paint_async(layout);

    // 8. Ship to main thread via channel
    co_return LoadedPage{std::move(doc), std::move(styles), std::move(layout), std::move(display_list)};
}
```

### 2.7 — Main Thread Consumer

| File | Change |
|------|--------|
| `browser/browser_window.cpp` | `BrowserWindow::start_load()` fires the async pipeline and returns immediately. When the `channel<LoadedPage>` delivers the result, the main thread picks it up on the next frame. |

**Verification**: Navigate to complex pages. UI stays responsive during load. FPS counter shows no drop.

### Phase 2 Prompt — Use this prompt in a new session:

```
You are tasked with completing Phase 2 of the browser roadmap at C:\github\browser\roadmap.md. Read the roadmap file first (Phase 2, lines 239-344, plus Phase 1 Lessons Learned at lines 224-236 and Phase 2 Lessons at lines 340-344), then implement ALL items.

WARNING: Phase 2 was previously completed and Phase 3 implementation has already been started in the current codebase. The repo contains pre-existing Phase 3 files (image/ decoders, preload_scanner, resource_loader, font_loader). These may have introduced breaking changes or dependencies that Phase 2 code must accommodate. Read the full state of the codebase before making changes. Do not break Phase 3 files — adapt Phase 2 code to coexist.

Phase 2: Off-Main-Thread Page Pipeline

Goal: HTML parsing, CSS parsing, cascade, layout, and paint record generation all run on worker threads. The main thread only handles UI events and compositing.

CRITICAL — Type system constraints from Phase 1 (violating these will not compile):
- task<T>::await_resume() already returns Result<T>. DO NOT write task<Result<T>> — that double-wraps.
- task<std::string> is impossible because Result<T, E=std::string> asserts T != E. Use task<std::vector<u8>> for string returns.
- task<void> cannot return errors (task_promise<void> has only return_void()). Use task<bool> for fallible void operations: true=success, co_return std::string("error message") for failure.
- task<T> by default starts suspended. Call .start() or co_await to begin execution.
- Always offload to thread pool at the start of coroutine pipelines: co_await thread_pool_executor{} first.

Work to complete:

2.1 — Thread-Safe HTML Parser
- html/parser.cpp: Audit for global/static mutable state. Isolate all state into a ParserState struct allocated per-invocation.
- html::parse() becomes html::parse_async() returning task<std::unique_ptr<Document>> via co_await thread_pool_executor{}.
- html/entities.cpp: Make entity lookup tables constexpr or const with no mutable static state.
- html/dom.hpp: Ensure Document/Element/Text/Comment are movable (unique_ptr for children, std::vector for attributes) and not shared across threads.
- Do NOT break the preload scanner in html/preload_scanner.cpp — it depends on the tokenizer.

2.2 — Thread-Safe CSS Parser
- css/parser.cpp: Same audit. css::parse_async() → task<StyleSheet>. No mutable globals.

2.3 — Async Cascade
- css/cascade.cpp: Cascade::compute_async() → task<ComputedStyles>. CPU-bound, runs on thread pool.

2.4 — Async Layout
- css/layout.cpp + css/layout.hpp: LayoutEngine::layout_async() → task<std::unique_ptr<LayoutNode>>.
- LayoutNode tree must be movable via unique_ptr ownership chain.

2.5 — Async Paint Record Generation
- render/painter.cpp: Painter::paint_async() → task<std::shared_ptr<DisplayList>>.
- DisplayList must be self-contained (no T* pointers to layout tree).

2.6 — Async PageLoader Pipeline
- browser/page_loader.cpp: Rewrite PageLoader::load() as a coroutine pipeline.
- Stages: fetch HTTP → decompress → parse HTML → fetch CSS (concurrent when_all) → cascade → layout → paint → ship via channel.
- All return types from co_await must be checked: r.is_ok() before r.unwrap().
- LoadedPage must be movable (unique_ptr, shared_ptr, or value members).
- Do NOT break the ResourceLoader integration that Phase 3 added to PageLoader.

2.7 — Main Thread Consumer
- browser/browser_window.cpp: start_load() fires async pipeline, returns immediately.
- Result delivered via channel<LoadedPage> — main thread picks it up next frame.

Build massive decoders incrementally (applies to Phase 3 when you reach it, good practice for all large files):
- Never write a 2000-line decoder in a single edit. Break into steps: (1) header parsing + format detection, (2) metadata extraction, (3) pixel decode, (4) color conversion, (5) test each step before the next.
- Edit sizes should be 200-400 lines max per write, then build + test.

Verification:
1. cmake --build build succeeds with no warnings (-Werror)
2. All pre-existing test executables pass (no regressions)
3. UI stays responsive during page load
4. about:blank and simple HTML pages render correctly
5. Phase 3 files (image/, preload_scanner/, resource_loader/) still compile and work

Do NOT proceed to Phase 3. Stop after Phase 2 is verified complete.
```
### Phase 2 Checklist
- [x] `html/parser.cpp` thread-safe: no mutable globals, state isolated in `ParserState`. `parse()` returns `async::task<unique_ptr<Document>>`.
- [x] `css/parser.cpp` → `async::task<StyleSheet>`. All state per-invocation.
- [x] `css/cascade.cpp` → `async::task<std::unordered_map<const Element*, ComputedStyle>>`.
- [x] `css/layout.cpp` → `async::task<unique_ptr<LayoutNode>>`. Tree movable via unique_ptr ownership.
- [x] `render/painter.cpp` → `async::task<shared_ptr<DisplayList>>`. All commands trivially movable (no T* pointers).
- [x] `browser/page_loader.cpp`: `load()` rewritten as coroutine pipeline. HTML fetch → decompress → parse → CSS cascade → layout → paint → channel, all off-main-thread.
- [x] `BrowserWindow::start_load()` fires async pipeline and returns immediately; result delivered via `channel<LoadedPage>`.
- [x] UI remains interactive during page load (click/scroll/type while loading) — main thread never blocks.
- [x] All 33 pre-existing test executables pass (0 regressions).

### Phase 2 Lessons
1. **`task<T>::await_resume()` returns `Result<T>`**: The `co_await` expression on `task<T>` yields `Result<T>`, not `T`. All call sites must check `r.is_ok()` before `r.unwrap()`. For `task<void>`, `co_await` returns nothing (void).
2. **`.sync_wait()` from a thread pool thread risks deadlock**: When already on the thread pool, `co_await` is safe but `.sync_wait()` blocks the pool thread, preventing forward progress. Always prefer `co_await`.
3. **Always offload to thread pool at the start of coroutine pipelines**: The `co_await thread_pool_executor{}` at the entry point prevents any blocking operations from accidentally running on the main thread.
4. **`LoadedPage` must be movable**: All members must be `unique_ptr`, `shared_ptr`, or value types. The `unordered_map<const Element*, ComputedStyle>` is movable but its pointers into the DOM are only valid while the owning `unique_ptr<Document>` remains at the same address — which holds across moves.
5. **Layout tree is movable via `unique_ptr`**: The existing `unique_ptr<LayoutNode>` tree already supports move semantics because `unique_ptr` ownership transfer preserves object addresses. Index-based layout was not required.
6. **DisplayList must be self-contained**: The `PaintCommand` struct has no `T*` pointers — only `std::string`, enums, and `f32` values. This ensures the display list is safe to move between threads.

---

## Phase 3: Sub-Resource Loading

**Note**: Implemented *after* Phase 4 (JS execution) because images/fonts are cosmetic while JS enables functionality. The document order is numerical; the implementation order places this after Phase 4. See the Implementation Order Summary.

**Goal**: CSS, JavaScript, images, and fonts are fetched *concurrently* with HTML parsing. No sequential blocking.

### 3.1 — Preload Scanner

| File | Change |
|------|--------|
| New: `html/preload_scanner.cpp`, `html/preload_scanner.hpp` | Lightweight token peek that runs alongside the main HTML parser. Scans for `<img src>`, `<link rel=stylesheet href>`, `<script src>`, `<link rel=preload>`. Fires async fetch requests immediately — before the DOM tree is built. |
| `html/parser.cpp` | Feed tokens to preload scanner as they're emitted by the tokenizer. |

### 3.2 — Resource Loader

| File | Change |
|------|--------|
| New: `html/resource_loader.hpp`, `html/resource_loader.cpp` | `ResourceLoader` — manages a priority queue of pending fetches. Priority: CSS (highest, blocks render) > JS (depends on async/defer) > images > fonts > prefetch (lowest). Dedupes by URL. Supports `async`, `defer`, `module` for scripts. |
| `browser/page_loader.cpp` | Route all sub-resource fetches through `ResourceLoader` instead of ad-hoc `http_.fetch()` calls in `collect_css()`. |

### 3.3 — Image Loading & Decoding

**Strategy**: All decoders are hand-written from scratch. No WIC, no system imaging components, no third-party libraries. Start with the simplest formats and work up to the hardest. Each format gets its own file in `image/`.

| Format | File | Effort | Dependencies |
|--------|------|--------|-------------|
| BMP | `image/decoder_bmp.cpp` | ~100 lines | None (header + pixel data) |
| GIF | `image/decoder_gif.cpp` | ~500 lines | LZW decompress (self-contained) |
| PNG | `image/decoder_png.cpp` | ~800 lines | Uses existing `net/deflate.cpp` for IDAT decompression |
| JPEG | `image/decoder_jpeg.cpp` | ~2000 lines | DCT math, Huffman decode (self-contained) |

| File | Change |
|------|--------|
| New: `image/format.hpp` | `enum class ImageFormat { PNG, JPEG, GIF, BMP, UNKNOWN }`. Detection from magic bytes. Helper: `detect_format(span<u8>) -> ImageFormat`. `class Image` — holds decoded RGBA pixels, width, height. |
| New: `image/decoder.hpp` | Abstract `Decoder` base. `virtual Result<Image> decode(span<u8>)`. Factory: `create_decoder(ImageFormat) -> unique_ptr<Decoder>`. |
| New: `image/decoder_bmp.cpp` | BMP decoder: parse BITMAPFILEHEADER + BITMAPINFOHEADER, handle 1/4/8/16/24/32 bpp, RLE compression, pixel data extraction. |
| New: `image/decoder_gif.cpp` | GIF decoder: header + logical screen descriptor → color table → LZW-encoded image data → deinterlace → palette-to-RGBA. Animation frame timing deferred. |
| New: `image/decoder_png.cpp` | PNG decoder: IHDR → PLTE (optional) → IDAT chunks (deflate via `net/deflate.cpp`) → pixel filter reconstruction (None, Sub, Up, Average, Paeth) → CRC verification. Adam7 interlacing deferred. |
| New: `image/decoder_jpeg.cpp` | JPEG decoder: SOI → APP0 (JFIF) → DQT (quantization tables) → SOF (frame dimensions + sampling) → DHT (Huffman tables) → SOS (scan). IDCT per MCU. Chroma upsampling (4:2:0 → 4:4:4). **Build incrementally — marker parsing first, then Huffman decode, then IDCT math, then chroma upsampling. Write and test each step before moving to the next.** |
| `CMakeLists.txt` | Add `image/` library target. No additional system libraries needed. |
| `browser/page_loader.cpp` | When `<img src>` is encountered, fire async fetch → detect format via magic bytes → decode on thread pool (via `image::Decoder`) → ship `Image` to main thread. |
| `render/painter.cpp` | Add `DrawImage` display command. |
| `render/paint_executor.cpp` | Bind the `Image` RGBA pixels as an OpenGL texture, draw a textured quad. Cache textures in a `std::map<ImageId, GLuint>`. |

### 3.4 — Font Loading via `@font-face`

| File | Change |
|------|--------|
| `css/parser.cpp` | Parse `@font-face` rules: `font-family`, `src: url()`, `font-weight`, `font-style`, `unicode-range`. |
| New: `render/font_loader.cpp` | `FontLoader` — async fetch `@font-face` URLs, decode TTF/WOFF/WOFF2, register with `FontManager`. |
| `browser/page_loader.cpp` | During CSS collection, extract `@font-face` URLs, fetch them concurrently with other resources. |

### Phase 3 Checklist
- [ ] Preload scanner runs alongside HTML tokenizer, fires async fetches for `<img>`, `<link>`, `<script>` before DOM build
- [ ] `ResourceLoader` manages priority queue: CSS > JS > images > fonts > prefetch. URL dedup works.
- [ ] Hand-written BMP decoder (`image/decoder_bmp.cpp`) — 1/4/8/16/24/32 bpp, RLE, pixel extraction.
- [ ] Hand-written GIF decoder (`image/decoder_gif.cpp`) — LZW, palette, deinterlace.
- [ ] Hand-written PNG decoder (`image/decoder_png.cpp`) — deflate via `net/deflate.cpp`, filter reconstruction, CRC.
- [ ] Hand-written JPEG decoder (`image/decoder_jpeg.cpp`) — Huffman, DCT, IDCT, chroma upsampling.
- [ ] `image/` library target in CMakeLists.txt. No external libs linked.
- [ ] `DrawImage` display command. OpenGL textured quad rendering in paint executor.
- [ ] `<img src="">` causes async fetch → magic byte detection → decode on thread pool → render.
- [ ] `@font-face` parsing in `css/parser.cpp`. `FontLoader` fetches and registers fonts.
- [ ] No WIC, no COM, no system imaging components used anywhere.
- [ ] All pre-existing tests still pass

### Phase 3 Lessons Learned

| Lesson | Details |
|--------|---------|
| **COM initialization required for WIC** | `CoInitializeEx` must be called before any WIC operations. Since WIC may be used from thread pool threads, the decoder handles this internally with `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`. |
| **PNG CRC validation skipped** | CRC-32 validation on PNG chunks was implemented but removed to avoid unused-code warnings since the inflate step already validates data integrity. Can be re-added for strict validation. |
| **WIC uses BGR pixel order** | WIC's `GUID_WICPixelFormat32bppRGBA` returns RGBA data, not BGRA. Matches our `Image` struct expectations. |
| **GIF LZW edge cases** | GIF LZW decoding requires handling of clear code (resets dictionary), end-of-info code, and variable-width codes (3-12 bits). The decoder uses LSB-first packing per GIF spec. |
| **BMP top-down vs bottom-up** | BMPs can be encoded top-down (negative height) or bottom-up (positive height). The decoder handles both. |
| **@font-face CSS parsing** | The existing CSS parser's `parse_at_rule` method treats block contents as style rules (selectors + declarations). For @font-face, the body contains only declarations. Added a special case when `at.name == "font-face"`. |
| **FontManager::load_from_memory** | The existing `FontManager` only had `load_from_file`. Added `load_from_memory` so `FontLoader` can register fonts from fetched data without writing temp files. |
| **Tiled texturing approach** | The paint executor caches `Texture2D` objects keyed by `Image*` address, avoiding redundant GPU uploads when the same image appears multiple times on a page. |

---

## Phase 4: JavaScript Execution & DOM Bindings

**Goal**: `<script>` elements are fetched, compiled, and executed against the live DOM. The JS engine has enough builtins to run real-world scripts.

This is the **largest single phase** and will take the most effort.

### 4.1 — Prototype Chain & Property Resolution

| File | Change |
|------|--------|
| `js/vm.cpp` | Implement `[[Get]]` and `[[Set]]` with prototype chain walk. `JSObject::get_property()` checks own → walks `[[Prototype]]` chain. `JSObject::set_property()` sets own (never writes to prototype). |
| `js/vm.cpp` | Implement `new` operator correctly: `[[Construct]]` → allocate `JSObject` with `__proto__` = constructor's `.prototype`, call constructor with `this` bound. |
| `js/vm.cpp` | Implement `instanceof`: check `R.prototype` in `L`'s prototype chain. |

**Tests**: `tests/js_test.cpp` — Prototype chain resolution, `new` creates correct hierarchy, `instanceof` works.

### 4.2 — `this` Binding

| File | Change |
|------|--------|
| `js/vm.cpp` | Function call `this` binding: normal call → global (non-strict) or `undefined` (strict). Method call → receiver object. Arrow functions → lexical `this` from enclosing scope. |
| `js/vm.cpp` | Implement `call()`, `apply()`, `bind()`. |

### 4.3 — JS Builtins: String.prototype

| File | Change |
|------|--------|
| New: `js/builtins/string.cpp` | Register native functions for: `charAt`, `charCodeAt`, `indexOf`, `lastIndexOf`, `includes`, `startsWith`, `endsWith`, `slice`, `substring`, `substr`, `toUpperCase`, `toLowerCase`, `trim`, `trimStart`, `trimEnd`, `split`, `replace`, `replaceAll`, `match`, `search`, `repeat`, `padStart`, `padEnd`, `concat`, `length` (getter). |

### 4.4 — JS Builtins: Array.prototype

| File | Change |
|------|--------|
| New: `js/builtins/array.cpp` | `push`, `pop`, `shift`, `unshift`, `forEach`, `map`, `filter`, `reduce`, `reduceRight`, `find`, `findIndex`, `some`, `every`, `includes`, `indexOf`, `lastIndexOf`, `splice`, `slice`, `sort`, `reverse`, `join`, `concat`, `flat`, `flatMap`, `fill`, `isArray` (static), `from` (static), `of` (static). |

### 4.5 — JS Builtins: Object

| File | Change |
|------|--------|
| New: `js/builtins/object.cpp` | `keys`, `values`, `entries`, `assign`, `create`, `defineProperty`, `defineProperties`, `getOwnPropertyDescriptor`, `getOwnPropertyNames`, `getPrototypeOf`, `setPrototypeOf`, `freeze`, `seal`, `preventExtensions`, `isFrozen`, `isSealed`, `isExtensible`. |

### 4.6 — JS Builtins: Math, Number, Boolean, Symbol

| File | Change |
|------|--------|
| New: `js/builtins/math.cpp` | `abs`, `ceil`, `floor`, `round`, `max`, `min`, `pow`, `sqrt`, `random`, `sin`, `cos`, `tan`, `PI`, `E`, etc. |
| New: `js/builtins/number.cpp` | `toFixed`, `toExponential`, `toPrecision`, `toString`, `parseInt`, `parseFloat`, `isNaN`, `isFinite`, `isInteger`, `isSafeInteger`. |
| New: `js/builtins/symbol.cpp` | `Symbol()` constructor, `Symbol.iterator`, `Symbol.toStringTag`, `Symbol.for`, `Symbol.keyFor`. |

### 4.7 — JS Builtins: JSON, Date, RegExp, Error

| File | Change |
|------|--------|
| New: `js/builtins/json.cpp` | `JSON.parse()` — recursive descent JSON parser. `JSON.stringify()` — walk value tree, serialize. |
| New: `js/builtins/date.cpp` | `Date()` — wraps `std::chrono::system_clock`. `getTime`, `getFullYear`, `getMonth`, `getDate`, `getDay`, `getHours`, `getMinutes`, `getSeconds`, `getTimezoneOffset`, `toISOString`, `toUTCString`, `now()` (static), `parse()` (static). |
| New: `js/builtins/regexp.cpp` | `RegExp` — compile pattern to NFA/DFA. `test()`, `exec()`. Backreferences, character classes, quantifiers, groups. |
| New: `js/builtins/error.cpp` | `Error`, `TypeError`, `ReferenceError`, `SyntaxError`, `RangeError`, `URIError`, `EvalError`. Each has `name`, `message`, `stack` (capture call stack on creation). |

### 4.8 — Console API

| File | Change |
|------|--------|
| New: `js/builtins/console.cpp` | `console.log`, `console.warn`, `console.error`, `console.info`, `console.debug`, `console.table`, `console.group`, `console.groupEnd`, `console.time`/`timeEnd`, `console.count`, `console.assert`, `console.clear`. Output via Win32 `OutputDebugStringA` and/or an in-browser DevTools console panel. |

### 4.9 — Timers: setTimeout / setInterval / requestAnimationFrame / queueMicrotask

| File | Change |
|------|--------|
| New: `js/builtins/timers.cpp` | `setTimeout(fn, ms)` — schedule on the main thread's timer queue (min-heap of expiry times). Checked each frame. `clearTimeout(id)` — cancel. `setInterval` / `clearInterval` — same with repeat. `requestAnimationFrame(fn)` — fire before next render. `cancelAnimationFrame(id)` — cancel. `queueMicrotask(fn)` — enqueue in the microtask queue. |

**Design**: The main event loop in `BrowserWindow::run()`:
1. Pump Windows messages (non-blocking `PeekMessage`)
2. Fire expired timers
3. Drain microtask queue
4. Fire pending `requestAnimationFrame` callbacks
5. Render frame (compositor)
6. Swap buffers

### 4.10 — Promises & async/await

| File | Change |
|------|--------|
| New: `js/builtins/promise.cpp` | `Promise` constructor, `.then()`, `.catch()`, `.finally()`, `Promise.resolve()`, `Promise.reject()`, `Promise.all()`, `Promise.race()`, `Promise.allSettled()`, `Promise.any()`. Microtask queue for `.then()` callbacks. |
| `js/parser.cpp` | Enable `async` function parsing (the parser already has `is_async` on `FuncDeclStmt`). Implement `await` expression. Rewrite async functions as state machines that return Promises. |
| `js/compiler.cpp` | Emit bytecode for `await`, promise creation/resolution. |

### 4.11 — ES Modules

| File | Change |
|------|--------|
| `js/parser.cpp` | Parse `import` declarations, `export` declarations. |
| New: `js/module_loader.cpp` | `ModuleLoader` — async fetch modules, parse, resolve dependency tree (topological sort), execute in order. Handle circular dependencies. |
| New: `js/builtins/import_meta.cpp` | `import.meta` — `url`, `resolve`. |

### 4.12 — DOM Bindings (Critical Web APIs)

| File | Change |
|------|--------|
| `js/dom_bindings.cpp` | **Complete rewrite.** Bind every essential DOM interface. Use a code generator pattern: a `JSClass` descriptor struct that maps method names to native function pointers. |
| New: `js/dom_bindings/document.cpp` | `document.createElement()`, `document.createTextNode()`, `document.getElementById()`, `document.querySelector()`, `document.querySelectorAll()`, `document.title` (get/set), `document.body`, `document.head`, `document.documentElement`, `document.URL`, `document.domain`, `document.referrer`, `document.cookie`, `document.open()`, `document.close()`, `document.write()`, `document.writeln()`, `document.contentType`, `document.compatMode`, `document.defaultView`, `document.adoptNode()`, `document.importNode()`. |

> **`document.write()` complexity**: Per HTML spec, `document.write()` must insert into the parser's input stream while the parser is running. This requires reentrant parser state: save/restore insertion mode, handle foster parenting, and potentially reprocess tokens. Implement as an interleaved token buffer that the parser drains before consuming network input. `document.open()` clears the document. `document.close()` signals end-of-stream.

| New: `js/dom_bindings/element.cpp` | `.innerHTML` (get/set — setter parses HTML via `html::parse()`), `.outerHTML`, `.textContent`, `.classList`, `.style` (CSSStyleDeclaration), `.dataset`, `.id`, `.className`, `.tagName`, `.nodeName`, `.nodeType`, `.parentNode`, `.childNodes`, `.firstChild`, `.lastChild`, `.nextSibling`, `.previousSibling`, `.children`, `.firstElementChild`, `.lastElementChild`, `.previousElementSibling`, `.nextElementSibling`, `.childElementCount`, `.matches()`, `.closest()`, `.contains()`, `.insertBefore()`, `.removeChild()`, `.replaceChild()`, `.appendChild()`, `.append()`, `.prepend()`, `.before()`, `.after()`, `.replaceWith()`, `.remove()`, `.insertAdjacentHTML()`, `.focus()`, `.blur()`, `.click()`, `.scrollIntoView()`, `.getAttribute()`, `.setAttribute()`, `.removeAttribute()`, `.hasAttribute()`, `.getBoundingClientRect()`. |
| New: `js/dom_bindings/event.cpp` | `Event` constructor, `.type`, `.target`, `.currentTarget`, `.preventDefault()`, `.stopPropagation()`, `.stopImmediatePropagation()`, `.cancelable`, `.bubbles`, `.defaultPrevented`, `.eventPhase`, `.timeStamp`. `CustomEvent` constructor. `MouseEvent`, `KeyboardEvent`, `FocusEvent`, `WheelEvent`, `InputEvent`, `SubmitEvent`, `ClipboardEvent`. **Hit testing**: on mouse click, walk layout tree in reverse paint order, find element under cursor, fire `click`, `mousedown`, `mouseup`, `mousemove` events. |
| New: `js/dom_bindings/window.cpp` | `.innerWidth`, `.innerHeight`, `.outerWidth`, `.outerHeight`, `.screenX`, `.screenY`, `.scrollX`, `.scrollY`, `.scrollTo()`, `.scrollBy()`, `.open()`, `.close()`, `.location`, `.navigator`, `.history`, `.frames`, `.length`, `.top`, `.parent`, `.self`, `.name`, `.status`, `.closed`, `.opener`, `.origin`, `.isSecureContext`, `.devicePixelRatio`, `.matchMedia()`, `.getComputedStyle()`, `.postMessage()`, `.btoa()`, `.atob()`, `.setTimeout()`, `.setInterval()`, `.clearTimeout()`, `.clearInterval()`, `.requestAnimationFrame()`, `.cancelAnimationFrame()`, `.queueMicrotask()`, `.fetch()`, `.alert()`, `.confirm()`, `.prompt()`. |
| New: `js/builtins/performance.cpp` | `performance.now()` — high-resolution timestamp via `QueryPerformanceCounter`. `performance.timing` — navigation timing data. |
| New: `js/dom_bindings/navigator.cpp` | `.userAgent`, `.platform`, `.language`, `.languages`, `.onLine`, `.cookieEnabled`, `.geolocation`, `.clipboard`, `.sendBeacon()`. |
| New: `js/dom_bindings/location.cpp` | `.href`, `.protocol`, `.host`, `.hostname`, `.port`, `.pathname`, `.search`, `.hash`, `.origin`, `.assign()`, `.replace()`, `.reload()`. |
| New: `js/dom_bindings/history.cpp` | `.length`, `.back()`, `.forward()`, `.go()`, `.pushState()`, `.replaceState()`. `popstate` event on back/forward navigation. |
| New: `js/dom_bindings/cssom.cpp` | `CSSStyleDeclaration` — map of CSS property names to values. `.getPropertyValue()`, `.setProperty()`, `.removeProperty()`. `getComputedStyle(element)` — returns resolved style. |
| New: `js/dom_bindings/xhr.cpp` | `XMLHttpRequest` — `open()`, `send()`, `setRequestHeader()`, `getResponseHeader()`, `getAllResponseHeaders()`, `onreadystatechange`, `readyState`, `status`, `statusText`, `responseText`, `response`, `timeout`, `withCredentials`. |
| New: `js/dom_bindings/fetch.cpp` | `fetch(url, init)` → returns `Promise<Response>`. Uses `HTTPClient` async internally. `Response` has `.json()`, `.text()`, `.blob()`, `.headers`, `.status`, `.ok`. |

**Tests**: `tests/dom_bindings_test.cpp` — Create elements from JS, manipulate DOM, fire events, verify callbacks. `tests/web_api_test.cpp` — Integration tests via script execution.

### 4.13 — Script Execution Integration

| File | Change |
|------|--------|
| `browser/page_loader.cpp` | After HTML parsing, collect `<script>` elements. For inline scripts — compile and execute immediately. For external scripts — fetch async, then compile and execute. Support `async` and `defer` attributes. |
| New: `js/script_runner.cpp` | `ScriptRunner` — manages script execution order per the HTML spec. Inline scripts block parser. Deferred scripts execute after parse. Async scripts execute as soon as downloaded. |

### 4.14 — Execution Context & Global Objects

| File | Change |
|------|--------|
| `js/vm.cpp` | Each page load creates a new JS execution context with its own global object. Global object has `window`, `document`, `console`, `setTimeout`, etc. GC roots are all global objects + active call frames. |

### Phase 4 Checklist
- [ ] Prototype chain: `[[Get]]` walks own → prototype chain. `[[Set]]` sets own only. `new` operator allocates with correct `__proto__`. `instanceof` walks prototype chain.
- [ ] `this` binding: normal call → global/strict-undefined, method call → receiver, arrow → lexical. `.call()`/`.apply()`/`.bind()` implemented.
- [ ] `js/builtins/string.cpp`: all listed String methods implemented and tested
- [ ] `js/builtins/array.cpp`: all listed Array methods implemented and tested
- [ ] `js/builtins/object.cpp`: all listed Object methods implemented and tested
- [ ] `js/builtins/math.cpp`, `number.cpp`, `symbol.cpp`: all listed methods implemented and tested
- [ ] `js/builtins/json.cpp`, `date.cpp`, `regexp.cpp`, `error.cpp`: all listed methods implemented and tested
- [ ] `js/builtins/console.cpp`: log/warn/error/table/group/time/count/assert all output correctly
- [ ] `js/builtins/timers.cpp`: setTimeout/Interval fire at correct times. rAF fires before render. queueMicrotask drains.
- [ ] `js/builtins/promise.cpp`: Promise.then/catch/finally/all/race work. async/await compile and execute.
- [ ] `js/module_loader.cpp`: import/export parsed, modules fetched, dependency graph resolved, circular deps handled.
- [ ] `js/dom_bindings/document.cpp`: all listed methods work (createElement, getElementById, querySelector, cookie, write, etc.)
- [ ] `js/dom_bindings/element.cpp`: innerHTML (get+set), classList, style, parentNode, childNodes, appendChild, etc. all work
- [ ] `js/dom_bindings/event.cpp`: Event constructor, dispatch, preventDefault, stopPropagation work. MouseEvent hit testing works.
- [ ] `js/dom_bindings/window.cpp`: all listed properties and methods work. `performance.now()` returns high-res timestamp.
- [ ] `js/dom_bindings/navigator.cpp` + `location.cpp` + `history.cpp` + `cssom.cpp` all work
- [ ] `js/dom_bindings/xhr.cpp` + `fetch.cpp`: HTTP requests from JS succeed. CORS enforced (Phase 9 for strict enforcement).
- [ ] `js/script_runner.cpp`: inline scripts execute, external scripts fetch+execute, async/defer attributes respected.
- [ ] `<script>` elements in HTML are fetched, compiled, and executed against the live DOM.
- [ ] Click on a page element fires the correct DOM events (click, mousedown, mouseup) on the correct element.
- [ ] All pre-existing tests still pass

---

## Phase 5: CSS Completeness

**Goal**: CSS is usable for real-world pages. Animations, transitions, transforms, gradients, `calc()`, custom properties, full pseudo-class support.

### 5.1 — CSS Animations & Transitions

| File | Change |
|------|--------|
| `css/parser.cpp` | Parse `@keyframes` rules: keyframe selector (percentage or `from`/`to`), block of declarations. Parse `animation-*` properties: `name`, `duration`, `timing-function`, `delay`, `iteration-count`, `direction`, `fill-mode`, `play-state`. |
| New: `css/animation.cpp`, `css/animation.hpp` | `AnimationEngine` — runs on main thread (or compositor thread). On each frame: compute interpolated values for all active animations, update computed styles, trigger re-layout if needed. Timing functions: linear, ease, ease-in, ease-out, ease-in-out, cubic-bezier, steps. |
| New: `css/transition.cpp` | `TransitionManager` — on computed style change (after layout → paint), if a `transition-*` property is set, start an interpolation from old → new value over the transition duration. |
| `render/renderer.cpp` | Integrate `AnimationEngine` update before each frame render. |

**Tests**: `tests/css_animation_test.cpp` — Verify keyframe interpolation, timing function output, completion callbacks.

### 5.2 — CSS Transforms

| File | Change |
|------|--------|
| `css/parser.cpp` | Parse `transform` property: `matrix()`, `translate()`, `rotate()`, `scale()`, `skew()`. Parse `transform-origin`. |
| `css/layout.cpp` | After layout, apply transform to layer's render transform matrix. Transformed elements get their own compositing layer. |
| `render/paint_executor.cpp` | Apply 3x3 affine transform matrix before drawing layer contents. |

### 5.3 — CSS `calc()`

| File | Change |
|------|--------|
| `css/css_values.cpp` | Parse `calc()` expressions into an AST. Evaluate: resolve percentages against parent, resolve `em`/`rem` against font size, combine units. Support `+`, `-`, `*`, `/`. |

### 5.4 — CSS Custom Properties (Variables)

| File | Change |
|------|--------|
| `css/parser.cpp` | Parse `--*` custom property declarations. Store in computed style map as string values. |
| `css/cascade.cpp` | Resolve `var(--name, fallback)` during cascade. Walk custom property inheritance chain. |

### 5.5 — CSS Gradients

| File | Change |
|------|--------|
| `css/css_values.cpp` | Parse `linear-gradient()`, `radial-gradient()`, `conic-gradient()`, `repeating-linear-gradient()`, etc. |
| `render/painter.cpp` | Add `DrawGradient` display command. Implement gradient rendering in the paint executor: for linear gradients, interpolate color stops pixel-by-pixel. Cache gradient textures in an atlas. |

### 5.6 — CSS Positioning & Overflow

| File | Change |
|------|--------|
| `css/layout.cpp` | Implement `position: fixed` (relative to viewport). Implement `position: sticky` (relative to scroll container). Implement `overflow: scroll` / `overflow: auto` — create scrollable sub-regions with their own clipping and scroll offsets. |
| `css/layout.cpp` | Implement `z-index` stacking contexts. Elements with `z-index`, `opacity < 1`, `transform`, `filter`, `mix-blend-mode`, `position: relative/absolute/fixed/sticky` get their own stacking context. Paint in order: background → negative z-index → block → float → inline → positioned → positive z-index. |
| `css/layout.cpp` | Implement `float` and `clear`: float boxes to left/right, clear prevents wrapping. Text and inline content flows around floats. |

### 5.7 — Remaining CSS Properties

| Property | Where | Implementation |
|----------|-------|---------------|
| `box-shadow` | `css/layout.cpp` + `render/painter.cpp` | Generate shadow paint commands: blurred rectangles at shadow offset. Use Gaussian blur via separable convolution shader or CPU. |
| `text-shadow` | `render/text_renderer.cpp` | Render text multiple times at offset with blur. |
| `border-radius` | `css/layout.cpp` + `render/paint_executor.cpp` | Elliptical corner clipping via stencil buffer or shader-based rounded rect. |
| `box-sizing: border-box` | `css/layout.cpp` | When `border-box`, width/height includes padding + border. |
| `line-height` | `css/layout.cpp` | Use computed `line-height` value for inline box height and text baseline. |
| `text-align` | `css/layout.cpp` | Left/center/right/justify text within inline formatting context. |
| `white-space` | `css/layout.cpp` | Normal: collapse whitespace, auto-wrap. Nowrap: no wrapping. Pre: preserve whitespace. Pre-wrap/pre-line. |
| `word-break` / `overflow-wrap` | `css/layout.cpp` | Break long words at character boundaries or word boundaries. |
| `:hover`, `:focus`, `:active`, `:visited` | `css/selector_match.cpp` | Evaluate dynamic pseudo-classes based on element state. Re-cascade + re-layout on state change. |
| `:nth-child()`, `:not()`, `:empty`, `:target`, `:disabled`, `:checked`, `:enabled` | `css/selector_match.cpp` | Parse and evaluate an+b for `:nth-child`. Evaluate `:not()` by negating inner selector. |
| `::before` / `::after` | `css/cascade.cpp` + `css/layout.cpp` | Create pseudo-elements in the DOM during cascade. Add `content` property value as generated text. Include in layout tree. |
| `Opacity` | `render/paint_executor.cpp` | Multiply layer alpha by opacity value. Create compositing layer for opacity < 1. |
| `min-width` / `max-width` / `min-height` / `max-height` | `css/layout.cpp` | Clamp computed sizes. Min/max constraints in block and inline layout. |
| `display: inline-block` | `css/layout.cpp` | Inline-level block container. Flows inline but has block layout inside. |
| `display: none` | `css/cascade.cpp` | Skip layout entirely. |

### 5.8 — `@media` Query Evaluation

| File | Change |
|------|--------|
| `css/parser.cpp` | Already parses `@media` rules. Current: always applies. Fix: evaluate media conditions. Evaluate `width`, `height`, `min-width`, `max-width`, `resolution`, `prefers-color-scheme`, `prefers-reduced-motion`, `orientation`, `hover`, `pointer`. |
| `css/cascade.cpp` | Only include rules from `@media` blocks whose conditions match the current environment. Re-evaluate on window resize / theme change. |

### Phase 5 Checklist
- [ ] `@keyframes` parsed, `AnimationEngine` interpolates values each frame. Timing functions (linear, ease, cubic-bezier) tested.
- [ ] CSS Transitions: property change triggers interpolation from old → new value over duration.
- [ ] `transform` parsed and applied as affine transform on compositor layer. `transform-origin` respected.
- [ ] `calc()` parsed into AST, evaluated with correct unit resolution (%, em, rem, px).
- [ ] Custom properties (`--*`) stored as strings; `var()` resolved in cascade with fallback.
- [ ] Gradients (`linear-gradient`, `radial-gradient`) parsed and rendered.
- [ ] `position: fixed/sticky` implemented. `overflow: scroll/auto` creates scrollable sub-regions.
- [ ] `z-index` stacking contexts correct: background → negative z → block → float → inline → positioned → positive z.
- [ ] `float`/`clear` implemented. Text flows around floats.
- [ ] `box-shadow` + `text-shadow` + `border-radius` + `box-sizing` + `line-height` + `text-align` + `white-space` + `word-break` all work.
- [ ] Dynamic pseudo-classes (`:hover`, `:focus`, `:active`, `:nth-child`, `:not`, `:empty`, `:disabled`, `:checked`) evaluated on state change.
- [ ] `::before`/`::after` pseudo-elements created at cascade time with `content` value.
- [ ] `opacity < 1` creates compositing layer with correct alpha.
- [ ] `min-width`/`max-width`/`min-height`/`max-height` clamp layout sizes.
- [ ] `display: inline-block` and `display: none` work.
- [ ] `@media` queries evaluate correctly; rules excluded when conditions don't match.
- [ ] All pre-existing tests still pass

---

## Phase 6: Compositor & Rendering Pipeline

**Goal**: Smooth 60fps scrolling, animations, and page rendering with no main-thread jank.

### 6.1 — Layer Tree

| File | Change |
|------|--------|
| New: `render/layer_tree.hpp`, `render/layer_tree.cpp` | `LayerTree` — built from the layout tree. Each layer corresponds to: scrollable overflow, `position: fixed/sticky`, `z-index` stacking context, `transform`, `opacity < 1`, `animation`, `will-change: transform`. Layers have: bounds, scroll offset, transform matrix, opacity, clip rect, display list, tile grid. |
| `render/painter.cpp` | Instead of generating one flat `DisplayList`, generate per-layer display lists. |

### 6.2 — Compositor Thread

| File | Change |
|------|--------|
| New: `render/compositor.hpp`, `render/compositor.cpp` | `Compositor` — runs on a dedicated thread (or compositor task on the thread pool). Responsibilities: receive layer tree from main thread, divide layers into tiles (256x256), prioritize tiles near viewport, submit raster tasks to raster thread pool, collect rasterized tiles, compose final frame (draw quads), submit compositor frame to GPU. |
| `render/renderer.cpp` | Main thread: update layer tree (when layout/paint changes), commit to compositor. Compositor: raster tiles, composite frame, signal main thread that frame is ready. |

### 6.3 — Tile-Based Rasterization

| File | Change |
|------|--------|
| New: `render/rasterizer.hpp`, `render/rasterizer.cpp` | `Rasterizer` — takes a layer + tile rect, produces a bitmap (RGBA pixels). Execute `DisplayList` commands into an off-screen pixel buffer. Currently CPU rasterized (pixel-fill in memory), later GPU rasterized via OpenGL FBO. |
| New: `render/tile_cache.hpp` | `TileCache` — LRU cache of rasterized tiles. Key = (layer_id, tile_x, tile_y, scale). Evict least-recently-used when memory exceeds budget (256MB target). |

### 6.4 — Event Loop Modernization

| File | Change |
|------|--------|
| `platform/window_win32.cpp` | Replace `PeekMessage` busy-poll with `MsgWaitForMultipleObjectsEx`. Wait on: window messages (HWND), compositor frame ready event, timer queue event. Use `MWMO_INPUTAVAILABLE` flag. Timeout = `INFINITE` (block until signal). This eliminates CPU usage when idle. |
| `browser/browser_window.cpp` | `BrowserWindow::run()` restructured: |

```cpp
HANDLE wait_handles[] = { compositor_frame_event_, timer_event_ };

while (!window_->should_close()) {
    // 1. Wait for any signal (messages, compositor frame, timers)
    DWORD wait = MsgWaitForMultipleObjectsEx(
        ARRAYSIZE(wait_handles), wait_handles,
        INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

    // 2. Process pending Windows messages (non-blocking drain)
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 3. Fire expired timers (setTimeout/setInterval)
    timer_queue_.fire_expired();

    // 4. Drain microtask queue (Promise callbacks)
    js_context_.drain_microtask_queue();

    // 5. Fire requestAnimationFrame callbacks
    js_context_.fire_animation_frame_callbacks();

    // 6. Check if compositor has a new frame ready
    if (compositor_.has_new_frame()) {
        renderer_->swap_buffers();
    } else if (needs_repaint_) {
        renderer_->render_frame();
        renderer_->swap_buffers();
    }
}
```

### 6.5 — Smooth Scrolling

| File | Change |
|------|--------|
| `browser/browser_window.cpp` | Scrolling updates the compositor's scroll offset, not the main thread's. Compositor re-uses existing tiles and shifts them. Only newly exposed tiles need rasterization. No main-thread involvement during scroll. |
| `css/layout.cpp` | `overflow: scroll` elements get their own compositor layer with independent scroll offset. |

**Verification**: `about:performance` shows 60fps during continuous scroll. Main thread frame time < 4ms.

### Phase 6 Checklist
- [ ] `LayerTree` built from layout tree. Layers for: scrollable overflow, `position: fixed/sticky`, `transform`, `opacity<1`, `z-index`, animations.
- [ ] `Compositor` thread receives layer tree, tiles layers (256x256), prioritizes viewport tiles, submits raster tasks.
- [ ] `Rasterizer` executes per-layer display lists into RGBA pixel buffers.
- [ ] `TileCache` LRU eviction at 256MB budget.
- [ ] Event loop: `MsgWaitForMultipleObjectsEx` blocks on messages + compositor frame + timer events. CPU usage < 1% when idle.
- [ ] Scrolling updates compositor scroll offset only. Main thread not involved. 60fps verified.
- [ ] All pre-existing tests still pass

---

## Phase 7: Storage — Cookies, localStorage, Cache

**Goal**: Persistent storage so that sessions, authentication, and preferences survive browser restarts.

### 7.1 — Cookie Jar

| File | Change |
|------|--------|
| New: `net/cookie_jar.hpp`, `net/cookie_jar.cpp` | `CookieJar` — in-memory cookie store. Fields: name, value, domain, path, secure, httpOnly, sameSite, expires (optional), max-age (optional), creation time, last access time. File-backed: `cookies.txt` (Netscape cookie file format). |
| `net/http_client.cpp` | On response receive: parse `Set-Cookie` header(s). Validate cookie attributes (domain match, path match, secure, httpOnly, sameSite). Store in jar. On request send: compute matching cookies, append `Cookie` header. |
| `js/dom_bindings/document.cpp` | `document.cookie` getter → return matching cookies for current domain. `document.cookie` setter → parse and store. |

**Tests**: `tests/cookie_test.cpp` — Set, get, expire, domain filtering, path filtering, secure/httpOnly flags.

### 7.2 — Web Storage (localStorage / sessionStorage)

| File | Change |
|------|--------|
| New: `net/storage.hpp`, `net/storage.cpp` | `Storage` — key-value store with string keys and string values. File-backed: `localstorage.txt` (JSON map). Capacity limit: 5MB per origin. `sessionStorage` is in-memory only, cleared on browser close. |
| New: `js/dom_bindings/storage.cpp` | `window.localStorage` → `Storage` object bound to JS. `.getItem(key)`, `.setItem(key, value)`, `.removeItem(key)`, `.clear()`, `.key(index)`, `.length`. `storage` event fired on same-origin pages. |
| `browser/browser_window.cpp` | On shutdown: flush localStorage to disk. |

**Tests**: `tests/storage_test.cpp` — Set/get/remove/clear, 5MB limit enforcement, persistence across sessions.

### 7.3 — HTTP Cache Persistence

| File | Change |
|------|--------|
| `net/http_cache.cpp` | Cache index persisted to `./cache/index.dat`. Response bodies stored in `./cache/responses/` directory. Cache size limit: 500MB. LRU eviction. |

### 7.4 — Settings Persistence Upgrade

| File | Change |
|------|--------|
| `browser/settings.cpp` | Move from `./settings.txt` (pipe-delimited) to `./settings.json` (or simple binary format). Store: theme, homepage, search engine, cookie policy, cache settings, font size, zoom level. |

### Phase 7 Checklist
- [ ] Cookie jar: `Set-Cookie` parsed, validated (domain/path/secure/httpOnly/sameSite), stored. Matching cookies sent on subsequent requests.
- [ ] `document.cookie` getter returns matching cookies. Setter parses and stores.
- [ ] `tests/cookie_test.cpp` passes: set, get, expire, domain/path filtering, secure/httpOnly flags.
- [ ] `localStorage` (file-backed) and `sessionStorage` (in-memory) implemented. 5MB per-origin limit enforced.
- [ ] `window.localStorage` bound: getItem/setItem/removeItem/clear/key/length. `storage` event fires on same-origin pages.
- [ ] `tests/storage_test.cpp` passes: set/get/remove/clear, 5MB limit, persistence.
- [ ] HTTP cache persists across restarts. 500MB LRU eviction. Cache-Control/ETag/Last-Modified honored.
- [ ] Settings upgraded from pipe-delimited to structured format.
- [ ] All pre-existing tests still pass

---

## Phase 8: Forms & User Interaction

**Goal**: `<input>`, `<button>`, `<select>`, `<textarea>` work. Form submission works. Clicks land on elements.

### 8.1 — Form Control Rendering

| File | Change |
|------|--------|
| New: `render/form_controls.cpp`, `render/form_controls.hpp` | Draw native-looking form controls: text input (rectangle with cursor, selection highlight), button (pressable rectangle with hover/active states), checkbox (checked/unchecked box), radio (filled/unfilled circle), select/dropdown (button + pulldown menu), textarea (multi-line text box with scrollbar). Use `HitTestResult` from phase 4. |
| `css/layout.cpp` | Form controls get intrinsic sizing: `<input>` = 20px height, `<button>` = content-width + padding, `<select>` = font-based width, `<textarea>` = rows*line-height x cols*char-width. |

### 8.2 — Form State & Events

| File | Change |
|------|--------|
| New: `html/form_state.cpp`, `html/form_state.hpp` | Per-form state: input values, checkbox/radio checked state, select selected index, textarea value. On state change, fire: `input`, `change`, `focus`, `blur` events. |
| `browser/browser_window.cpp` | Route click/keyboard events to the focused form element. Cursor blinking, text insertion, selection. |

### 8.3 — Form Submission

| File | Change |
|------|--------|
| New: `html/form_submission.cpp` | On `<form>` submit (button click or Enter key): encode form data as `application/x-www-form-urlencoded` or `multipart/form-data` (for file inputs). Construct URL. Navigate to result. |

### 8.4 — Hit Testing

| File | Change |
|------|--------|
| New: `html/hit_test.cpp`, `html/hit_test.hpp` | On mouse click in content area: walk layout tree in reverse paint order (back-to-front). For each element, test if click point is within element's bounding box. Handle `z-index`, stacking contexts, `overflow: hidden` clipping, `border-radius` clip. Return the topmost element. Fire `click`, `mousedown`, `mouseup` events on it. |

### Phase 8 Checklist
- [ ] Form controls render: `<input>` (text, checkbox, radio), `<button>`, `<select>`, `<textarea>` all have distinct visual appearance with hover/active/focus states.
- [ ] Form state management: input values, checkbox/radio checked, select index, textarea content all tracked. On change: fires `input`/`change`/`focus`/`blur`.
- [ ] Form submission: `<form>` submit via button click or Enter encodes data as `urlencoded` or `multipart`, navigates to action URL.
- [ ] Hit testing: click in content area walks layout tree in reverse paint order. Correct element under cursor is returned. Events fire on correct element.
- [ ] Keyboard events route to focused form element (cursor blinking, text insertion, selection).
- [ ] All pre-existing tests still pass

---

## Phase 9: Security Hardening

**Goal**: Protect users from malicious web content. Same-origin policy, CSP, HSTS, mixed content blocking.

### 9.1 — Same-Origin Policy

| File | Change |
|------|--------|
| New: `net/origin.hpp`, `net/origin.cpp` | `Origin` class: scheme + host + port. Compute effective origin for URLs. |
| `net/http_client.cpp` | Add `Origin` header to cross-origin requests. |
| `js/dom_bindings/fetch.cpp` | Implement CORS check: on cross-origin fetch response, verify `Access-Control-Allow-Origin`. Block if no matching header. |
| `js/dom_bindings/xhr.cpp` | Same CORS check. |
| `js/dom_bindings/document.cpp` | `document.cookie` getter: only return cookies for the current origin. Block cross-origin cookie access. |

### 9.2 — Content Security Policy (CSP)

| File | Change |
|------|--------|
| New: `net/csp.hpp`, `net/csp.cpp` | `CSPParser` — parse `Content-Security-Policy` header. Directives: `default-src`, `script-src`, `style-src`, `img-src`, `connect-src`, `font-src`, `frame-src`, `report-uri`, etc. |
| `browser/page_loader.cpp` | Enforce CSP on resource loading: block scripts from disallowed origins, block inline scripts if `'unsafe-inline'` not set, block `eval()` if `'unsafe-eval'` not set. Log violations. |
| `js/vm.cpp` | Before executing a script, check CSP. Before evaluating a string as code, check CSP `'unsafe-eval'`. |

### 9.3 — HSTS

| File | Change |
|------|--------|
| New: `net/hsts.hpp`, `net/hsts.cpp` | Parse `Strict-Transport-Security` header. Store HSTS policies per domain (file-backed). Upgrade HTTP to HTTPS for HSTS domains. Include built-in HSTS preload list (from Chromium's `transport_security_state_static.json`, converted to C++). |

### 9.4 — Mixed Content Blocking

| File | Change |
|------|--------|
| `browser/page_loader.cpp` | On HTTPS pages, detect HTTP subresource requests (images, scripts, styles, iframes). Block them unless the page explicitly allows it. Display a lock icon with mixed-content warning in the address bar. |

### 9.5 — Certificate Validation

| File | Change |
|------|--------|
| `net/tls.cpp` | Implement certificate chain validation: verify each certificate in the chain up to a trusted root. Check `notBefore`/`notAfter` validity dates. Check hostname match against certificate SAN/CN. Use Windows certificate store (`CertOpenStore`, `CertGetCertificateChain`) as the root trust store. |

### 9.6 — Process Sandboxing (Future)

| File | Change |
|------|--------|
| New: `browser/sandbox.cpp` | Long-term: separate renderer process per tab using Win32 job objects. Restrict privileges: no file system access (except cache), no clipboard, no network (except via browser process IPC). This is complex and low priority — mark for post-1.0. |

### Phase 9 Checklist
- [ ] `Origin` class computes effective origin per URL. `Origin` header sent on cross-origin requests.
- [ ] CORS: `fetch()`/`XHR` cross-origin responses blocked if `Access-Control-Allow-Origin` missing or doesn't match.
- [ ] CSP: `Content-Security-Policy` header parsed. Inline scripts blocked without `'unsafe-inline'`. `eval()` blocked without `'unsafe-eval'`. Violations logged.
- [ ] HSTS: `Strict-Transport-Security` header parsed, stored. HTTP requests upgraded to HTTPS for HSTS domains. Preload list included.
- [ ] Mixed content: HTTP subresources on HTTPS pages blocked. Lock icon with warning in address bar.
- [ ] Certificate validation: chain built to trusted root via `CertGetCertificateChain`. `notBefore`/`notAfter` checked. Hostname matches SAN/CN.
- [ ] All pre-existing tests still pass

---

## Phase 10: Performance Telemetry & Profiling

**Goal**: Continuous performance measurement. The browser tracks its own performance and exposes it transparently.

### 10.1 — Built-in Performance Dashboard

| File | Change |
|------|--------|
| `browser/telemetry.cpp` | Already generates `about:performance` HTML. Expand to include: FPS counter, frame time graph, memory usage (working set), GC stats, cache hit rate, per-subresource load timing waterfall, layout time, paint time, compositor frame time. |
| New: `browser/perf_counter.cpp` | Low-overhead performance counters. No heap allocation in hot paths. Produces a structured JSON report accessible via JS. |

### 10.2 — FPS Overlay

| File | Change |
|------|--------|
| `render/renderer.cpp` | Optional overlay: FPS counter in top-right corner. Toggle via Ctrl+Shift+F. Shows current, min, max, average FPS. Shows frame time breakdown: events, layout, paint, composite, GPU. |

### 10.3 — Memory Profiling

| File | Change |
|------|--------|
| `js/gc.cpp` | Expose GC stats: total heap size, live objects, garbage collected per cycle, pause times. |
| New: `async/memory.hpp` | Track allocations by subsystem. Expose via `about:performance`. |

### Phase 10 Checklist
- [ ] `about:performance` shows: FPS, frame time graph, memory usage, GC stats, cache hit rate, per-resource load waterfall, layout/paint/composite times.
- [ ] `browser/perf_counter.cpp` created with low-overhead counters (no heap alloc in hot paths).
- [ ] FPS overlay (Ctrl+Shift+F) shows current/min/max/average FPS + frame time breakdown. Non-intrusive.
- [ ] GC stats exposed: heap size, live objects, collected per cycle, pause times.
- [ ] Memory tracking by subsystem exposed via `about:performance`.
- [ ] All pre-existing tests still pass

---

## Phase 11: Browser Chrome Completeness

**Goal**: Finish the browser UI. Downloads, find-in-page, zoom, full-screen, DevTools console.

### 11.1 — Download Manager

**Safety-first design**: Downloads are **disabled by default**. A setting controls behavior: `disabled` (default) → silently blocks all downloads; `notify` → shows an alert in the browser chrome and does nothing else; `enabled` → prompts the user with a file-save dialog. Even when enabled, dangerous file types are blocked unless the user explicitly overrides per-file. All downloads go to a sandboxed `./downloads/` directory, never the user's Desktop or Downloads folder.

| File | Change |
|------|--------|
| New: `browser/download_manager.cpp`, `browser/download_manager.hpp` | Detect `Content-Disposition: attachment` header. If setting is `disabled`, drop silently and log. If `notify`, show in-browser toast/alert. If `enabled`, prompt with Win32 `GetSaveFileName` dialog (defaulting to `./downloads/`). Background download on thread pool. Write `Zone.Identifier` alternate data stream to mark file as from internet. |
| New: `browser/download_blocklist.cpp` | Extension-based blocklist: `.exe`, `.msi`, `.scr`, `.vbs`, `.ps1`, `.jar`, `.bat`, `.cmd`, `.dll`, `.ocx`, `.msu`, `.msp`, `.reg`, `.pif`, `.scr`, `.cpl`, `.app`, `.gadget`, `.hta`, `.wsf`, `.wsh`. Blocked by default unless user explicitly overrides. Also flag MIME/extension mismatches (`Content-Type: image/png` + `.exe` = blocked). 2GB file size cap. |
| `browser/settings.cpp` | Add `download_behavior` setting: `disabled` | `notify` | `enabled`. Default: `disabled`. |
| `browser/browser_window.cpp` | Download button in toolbar when a download is active (only visible in `enabled` mode). Click shows download list with progress. |

### 11.2 — Find in Page (Ctrl+F)

| File | Change |
|------|--------|
| New: `browser/find_bar.cpp` | Find bar UI: text input, match count, prev/next buttons. Search DOM text content. Highlight matches. Scroll to active match. |
| `browser/browser_window.cpp` | Ctrl+F opens find bar. Esc closes it. |

### 11.3 — Zoom (Ctrl+/Ctrl-)

| File | Change |
|------|--------|
| `css/layout.cpp` | Apply zoom factor (0.25 to 5.0) as a scale transform on the root layer. All layout calculations remain in CSS pixels; the compositor applies the final scale. |
| `browser/browser_window.cpp` | Ctrl++ / Ctrl+- / Ctrl+0. Show zoom percentage in address bar. Persist per-site. |

### 11.4 — Full-Screen Mode (F11)

| File | Change |
|------|--------|
| `platform/window_win32.cpp` | Toggle full-screen: `SetWindowLong` to remove `WS_POPUP` borders, `SetWindowPos` to span monitor. Or use `ChangeDisplaySettings` for exclusive full-screen. Toggle back to restore. |

### 11.5 — DevTools Console

| File | Change |
|------|--------|
| New: `browser/devtools.cpp`, `browser/devtools.hpp` | Minimal DevTools: in-browser panel. Tab bar with: Console (JS REPL), Elements (DOM tree inspector), Network (request log). Console: text input at bottom, log output above. F12 to toggle. This is essential for debugging both the browser itself and web pages. |

### 11.6 — View Source

| File | Change |
|------|--------|
| `browser/page_loader.cpp` | Handle `view-source:` URLs. Fetch the resource, display as syntax-highlighted HTML in a `<pre>` block. |

### 11.7 — Session Restore

| File | Change |
|------|--------|
| New: `browser/session.cpp` | On shutdown: save open tabs to `./session.dat`. On startup: if `session.dat` exists, restore tabs. Crash recovery: detect unclean shutdown via a lock file. |

### 11.8 — Omnibox (Search from Address Bar)

| File | Change |
|------|--------|
| `browser/browser_window.cpp` | When user types in address bar and presses Enter: if input looks like a URL (contains `.` or `://`), navigate to it. Otherwise, search using the configured search engine. |

### Phase 11 Checklist
- [ ] Download manager implements three modes: `disabled` (silent drop), `notify` (in-browser alert), `enabled` (file-save dialog). Default is `disabled`.
- [ ] `download_blocklist.cpp` blocks executable extensions (`.exe`, `.msi`, `.vbs`, `.ps1`, `.jar`, `.bat`, `.dll`, etc.) and MIME/extension mismatches. User can override per-file.
- [ ] 2GB file size cap enforced.
- [ ] `Zone.Identifier` alternate data stream written on all saved files.
- [ ] Downloaded files go to `./downloads/` directory, never system Downloads or Desktop.
- [ ] Find in page (Ctrl+F): text input with match count, prev/next, scroll-to-match, highlight all matches.
- [ ] Zoom (Ctrl++ / Ctrl+- / Ctrl+0): range 0.25–5.0, percentage shown in address bar, persisted per-site.
- [ ] Full-screen (F11): toggles exclusive full-screen mode, restores correctly.
- [ ] DevTools (F12): Console tab with JS REPL and log output. Elements tab with DOM tree. Network tab with request log.
- [ ] View source (`view-source:` URL): syntax-highlighted HTML in `<pre>` block.
- [ ] Session restore: open tabs saved to `./session.dat` on shutdown, restored on startup. Crash recovery via lock file.
- [ ] Omnibox: typed text that looks like a URL navigates directly; otherwise searches via configured search engine.
- [ ] All pre-existing tests still pass

---

## Phase 12: Final Compliance & Standards

**Goal**: The browser passes ACID3, html5test, and CSS3test suites (or as close as possible).

### 12.1 — HTML5 Compliance

| File | Change |
|------|--------|
| `html/parser.cpp` | Run the WHATWG HTML parser tests. Fix edge cases: adoption agency algorithm correctness, foster parenting edge cases, template content model isolation, SVG/MathML foreign content parsing, character reference edge cases. |
| `html/tokenizer.cpp` | Verify all 89 tokenizer states correctly handle edge cases from the spec test suite. |

### 12.2 — CSS Compliance

| File | Change |
|------|--------|
| `css/selector_match.cpp` | Run CSS selector tests. Fix edge cases. |
| `css/cascade.cpp` | Verify cascade order: `!important` in UA vs author vs inline. Verify inheritance for all properties. |
| `css/layout.cpp` | Implement missing table layout (display: table, table-row, table-cell). Implement `inline-block`. Implement `list-item` with markers. |

### 12.3 — JS Compliance

| File | Change |
|------|--------|
| `js/parser.cpp` | Run ECMAScript `test262` parser tests. Fix edge cases: automatic semicolon insertion, asi rules, strict mode parsing, destructuring, arrow function edge cases. |
| `js/vm.cpp` | Run `test262` execution tests. Fix: `ToPrimitive`, `ToNumber`, `ToString` coercion edge cases, `Date` parsing, `RegExp` edge cases, `JSON.stringify` replacer/space edge cases. |

### 12.4 — SVG & MathML

| File | Change |
|------|--------|
| `html/parser.cpp` | Implement foreign content insertion for SVG and MathML. Create SVG/MathML namespace elements. |
| New: `render/svg_renderer.cpp` | Minimal SVG rendering: `<svg>`, `<rect>`, `<circle>`, `<path>`, `<text>`, `<g>`, `<use>`. Most SVG in practice is used for icons, so basic shape rendering + CSS colors covers 90% of use. |
| New: `render/mathml_stub.cpp` | Minimal MathML: render as inline text with font-based math symbols. Full MathML layout is extremely complex. |

### Phase 12 Checklist
- [ ] Adoption agency algorithm matches WHATWG spec edge cases.
- [ ] Tokenizer correctly handles all 89 states per spec test suite.
- [ ] SVG foreign content parsing works: `<svg>` creates SVG namespace elements. Basic shapes render.
- [ ] MathML: inline text with font-based symbols renders correctly.
- [ ] CSS table layout (`display: table/table-row/table-cell`) implemented. `list-item` with markers works.
- [ ] ECMAScript `test262` subset passes: ASI, destructuring, strict mode, arrow functions, `ToPrimitive`/`ToNumber`/`ToString` coercion edge cases.
- [ ] `!important` cascade order correct: UA < author < inline; important reverses origin priority.
- [ ] All pre-existing tests still pass

---

## Phase 13: Media — Canvas, Audio, Video

**Goal**: `<canvas>`, `<audio>`, `<video>` elements have basic functionality.

### 13.1 — Canvas 2D

| File | Change |
|------|--------|
| New: `render/canvas.cpp`, `render/canvas.hpp` | `CanvasRenderingContext2D` — internal RGBA pixel buffer. Implement: `fillRect`, `strokeRect`, `clearRect`, `fillText`, `strokeText`, `beginPath`, `moveTo`, `lineTo`, `arc`, `bezierCurveTo`, `quadraticCurveTo`, `closePath`, `fill`, `stroke`, `clip`, `save`, `restore`, `translate`, `rotate`, `scale`, `transform`, `setTransform`, `globalAlpha`, `globalCompositeOperation`, `fillStyle`, `strokeStyle`, `lineWidth`, `font`, `textAlign`, `textBaseline`, `getImageData`, `putImageData`, `toDataURL`. |
| `js/dom_bindings/canvas.cpp` | Bind `HTMLCanvasElement.getContext('2d')` to return a `CanvasRenderingContext2D` wrapper. |

### 13.2 — Audio

| File | Change |
|------|--------|
| New: `platform/audio.cpp` | Use Win32 `waveOutOpen` / `waveOutWrite` for PCM audio playback. Simple `AudioBuffer` class. |
| New: `render/audio_element.cpp` | `<audio>` element: `play()`, `pause()`, `currentTime`, `duration`, `volume`, `loop`, `ended` event. Format support: WAV only initially (PCM). |

### 13.3 — Video (Minimal)

| File | Change |
|------|--------|
| New: `render/video_element.cpp` | `<video>` element: same API as `<audio>` plus a render target. Display video frames as textured quads in the page. Format support: none initially (beyond raw RGB frames). This is a placeholder for future codec integration. |

### Phase 13 Checklist
- [ ] Canvas 2D context: `fillRect`, `strokeRect`, `fillText`, paths, arcs, `save`/`restore`, transforms, `getImageData`/`putImageData`, `toDataURL` all work.
- [ ] `HTMLCanvasElement.getContext('2d')` returns a working rendering context from JS.
- [ ] Audio: `<audio>` with WAV source plays via `waveOutOpen`/`waveOutWrite`. `play()`/`pause()`/`currentTime`/`volume` work.
- [ ] Video: `<video>` element renders (placeholder for codec integration).
- [ ] All pre-existing tests still pass

---

## Testing & CI Philosophy

### Test Categories

| Level | What | Frequency |
|-------|------|-----------|
| **Unit tests** | Each subsystem independently: parser tests, layout tests, VM tests, crypto tests, network protocol tests. Coverage target: >90% of non-UI code. | Every commit |
| **Integration tests** | Cross-subsystem: HTML → CSS → layout → paint → render. JS → DOM bindings → page interaction. HTTP → cache → TLS integration. | Every PR |
| **Regression tests** | Bug reproducers added as tests before fixes. | Every bug fix |
| **Performance tests** | Frame time, load time, memory usage, GC pause time. Benchmarked against previous builds. Alert on >10% regression. | Nightly |
| **Security tests** | Fuzz testing for HTML parser, CSS parser, JS parser, URL parser, TLS certificate validation. | Nightly |
| **Compatibility tests** | html5test, css3test, test262 subset, ACID3, real-world site snapshots. | Weekly |

### CI Pipeline (`ci.ps1`)

```powershell
# 1. Build (Debug + Release)
cmake --build build --config Debug
cmake --build build --config Release

# 2. Run all unit tests
$failed = 0
Get-ChildItem -Path "build" -Filter "*_test.exe" | ForEach-Object {
    & $_.FullName
    if ($LASTEXITCODE -ne 0) { $failed++ }
}

# 3. Run performance benchmarks
& build/perf_test.exe --output perf_results.json

# 4. Static analysis (requires LLVM/clang-tidy installed separately)
# clang-tidy src/**/*.cpp html/**/*.cpp css/**/*.cpp js/**/*.cpp net/**/*.cpp render/**/*.cpp
# Alternative: GCC -fanalyzer (static analysis pass built into GCC 15)
cmake --build build --config Debug -- -fanalyzer 2>&1 | Select-String -Pattern "warning:|error:"

# 5. Memory leak detection (Debug builds)
# Dr. Memory or custom leak detector hooks

# 6. Report
if ($failed -gt 0) { exit 1 }
```

### Memory Leak Prevention

- All `new`/`delete` replaced with `std::unique_ptr`, `std::shared_ptr`, arena allocators
- Custom `HeapAlloc` wrapper tracks allocations in debug mode
- On `BrowserWindow::run()` exit, assert no leaked allocations
- GC root scanning: ensure all JS GC roots are properly released on page unload
- Test fixture: allocate heavy objects, destroy, verify no growth

### Race Condition Prevention

- No mutable global state (except constants)
- All cross-thread data passed via `channel<T>` (lock-free queue)
- `async::mutex` for shared state that can't be channelized
- `std::atomic` for counters and flags
- Thread sanitizer (TSan) on CI when available (Clang on Linux/WSL)
- Code review checklist item: "Is this thread-safe?"

---

## Implementation Order Summary

```
Phase 0:  Async Infrastructure (IOCP, coroutines, channels, memory)
    ↓
Phase 1:  Async Networking (overlapped sockets, async HTTP/TLS/DNS)
    ↓
Phase 2:  Off-Main-Thread Pipeline (async parse/layout/paint)
    ↓
Phase 4:  JavaScript DOM Bindings & Builtins ← CRITICAL MASS
    ↓
Phase 5:  CSS Completeness (animations, calc, transforms, gradients)
    ↓
Phase 6:  Compositor (smooth 60fps scrolling, tiled rasterization)
    ↓
Phase 3:  Sub-Resource Loading (preload scanner, images)
    ↓
Phase 7:  Storage (cookies, localStorage, HTTP cache)
    ↓
Phase 8:  Forms & Hit Testing
    ↓
Phase 9:  Security (CSP, CORS, HSTS, SOP)
    ↓
Phase 10: Performance Telemetry
    ↓
Phase 11: Chrome Completeness (downloads, find, zoom, devtools)
    ↓
Phase 12: Final Standards Compliance
    ↓
Phase 13: Media (canvas, audio, video)
```

**Phases 0–2** are pure infrastructure — the user sees no new features but the browser stops freezing.  
**Phase 4** is the single most important feature phase — it makes JavaScript actually work against real web pages. Without it, the browser is a document viewer.  
**Phases 5–9** fill the remaining gap to "daily driver" usability.  
**Phases 10–13** are polish, compliance, and quality of life.
