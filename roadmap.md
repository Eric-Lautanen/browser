# Browser Roadmap: From-scratch C++20 Web Browser

## Philosophy

This is a **from-scratch, zero-dependency** web browser written in **pure C++20** for Windows. It has **no third-party libraries** — every line of HTML parsing, CSS layout, JavaScript execution, TLS encryption, HTTP/2 multiplexing, font rasterization, and OpenGL rendering is hand-written. This is both the project's greatest strength and its biggest challenge.

### Design Principles

| Principle | Commitment |
|-----------|-----------|
| **Zero dependencies** | No Boost, Qt, WebKit, Chromium, libcurl, OpenSSL, zlib, Freetype, or any other third-party library. Only the C++ standard library, Win32 API (kernel32, user32, gdi32, ws2_32, bcrypt, opengl32), and OpenGL. No WIC, no COM, no system imaging components — every decoder is hand-written from scratch. |
| **Modern C++20** | RAII, move semantics, `std::unique_ptr`/`std::shared_ptr`, `std::optional`, `std::variant`, `std::string_view`, concepts, coroutines (where appropriate), `constexpr` where possible. No exceptions (`-fno-exceptions`). |
| **Simple & clean** | Minimal settings. No bloat. Every feature must earn its place. |
| **Performance-first** | Async I/O via IOCP. Off-main-thread parsing/layout/paint. GPU-accelerated rendering. Smooth 60fps scrolling with a compositor. No busy-wait event loops. |
| **Secure by default** | TLS 1.3 only. Same-origin policy. CSP enforcement. No telemetry. No tracking. No phoning home. HSTS preload. Certificate validation. |
| **No telemetry, ever** | The existing `telemetry.cpp` generates a local `about:performance` report only. It never sends data anywhere. This is non-negotiable. |
| **Standards compliant** | HTML5 (WHATWG living standard), CSS (W3C), ECMAScript (latest), Web APIs (WHATWG). Full compliance, not "good enough." |
| **Audited correctness** | No memory leaks. No race conditions. Every subsystem has comprehensive tests. Every merge is tested. |
| **Modular design** | Each major subsystem lives in its own folder (`html/`, `css/`, `js/`, `net/`, `render/`, `image/`, `platform/`, `async/`, `browser/`) with a clear public API. No circular dependencies. Each folder is a separate CMake library target with its own test executable. Easy to build, test, and maintain independently. New subsystems follow the same pattern. |
| **Web research on stuck tasks** | If an implementation step is unclear or blocked, search the web for current solutions before writing code — C++20/23 standards updates, Win32 API patterns, protocol specs, security guidance, or known pitfalls. Do not guess or rely on pre-2025 knowledge when the web has a better answer. This applies to both human and AI contributors. |
| **Living roadmap** | On completion of each phase, the roadmap is updated with lessons learned, corrected estimates, and refined scope. A second pass audit is performed before the next phase begins — verifying that the completed phase actually works as intended and that the remaining roadmap stays accurate and realistic. |

### Current State (June 2026)

- **Build**: CMake 3.20+, GCC 15.2+, Ninja, Windows (x86-64)
- **Working**: HTML5 tokenizer/parser/DOM, CSS parser/cascade/layout (block, flexbox, grid, position, float, z-index, overflow), JS lexer/parser/bytecode-compiler/VM/GC/JIT, HTTP/1.1, HTTP/2, TLS 1.3, DNS (UDP), OpenGL renderer, TrueType fonts, custom chrome UI, CSS animations/transitions, transforms, calc(), custom properties, gradients, @media queries, pseudo-classes, box-shadow, border-radius
- **Refactoring Complete**: All 13 structural refactor phases finished. Every large file split into focused sub-modules under dedicated directories. No single logic file exceeds ~600 lines.
- **Known gaps**: images not loaded, forms inert, no cookies/storage, no hit testing, no event dispatch to page elements

## Testing & CI

### Test Categories

| Level | What | Frequency |
|-------|------|-----------|
| **Unit tests** | Each subsystem independently: parser tests, layout tests, VM tests, crypto tests, network protocol tests. Coverage target: >90% of non-UI code. | Every commit |
| **Integration tests** | Cross-subsystem: HTML → CSS → layout → paint → render. JS → DOM bindings → page interaction. HTTP → cache → TLS integration. | Every PR |
| **Regression tests** | Bug reproducers added as tests before fixes. | Every bug fix |
| **Performance tests** | Frame time, load time, memory usage, GC pause time. Benchmarked against previous builds. Alert on >10% regression. | Nightly |
| **Security tests** | Fuzz testing for HTML parser, CSS parser, JS parser, URL parser, TLS certificate validation. | Nightly |

### CI Pipeline (`ci.ps1`)

```powershell
# 0. Configure (generate compile_commands.json for clang-tidy)
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B build

# 1. Build (Debug)
cmake --build build --config Debug

# 2. Run all unit tests
$failed = 0
Get-ChildItem -Path "build" -Filter "*_test.exe" | ForEach-Object {
    & $_.FullName
    if ($LASTEXITCODE -ne 0) { $failed++ }
}

# 3. Format check (only staged changes, skips non-C++ files automatically)
git clang-format --diff --quiet 2>&1 | Select-String -Pattern "modif|error"

# 4. Static analysis
clang-tidy --extra-arg=-Wno-unused-command-line-argument -p build html/*.cpp css/*.cpp js/*.cpp net/*.cpp render/*.cpp browser/*.cpp image/*.cpp async/*.cpp 2>&1 | Select-String -Pattern "warning:|error:"

# 5. Report
if ($failed -gt 0) { exit 1 }
```

### Code Quality Enforcement

- **clang-format**: All C++ files formatted. Check via `git clang-format --diff --quiet`.
- **clang-tidy**: Static analysis on all new/changed code. Zero warnings enforced.
- **-Werror**: All warnings treated as errors. Build must be completely clean.
- **Memory**: Custom `HeapAlloc` wrapper tracks allocations in debug mode. Zero leaks on exit.
- **Thread safety**: No mutable global state. Cross-thread data via `channel<T>`. `async::mutex` for shared state. `std::atomic` for counters and flags.

### Build Requirements

- CMake 3.20+
- GCC 15.2+ (with `-fcoroutines` for C++20 coroutines)
- Ninja build system
- Windows SDK (x86-64)

---

## Future Phases

*To be determined — this stub replaces the completed roadmap.*

| # | Phase | Status |
|---|-------|--------|
|   | *Next phase TBD* | Not started |

### Phase Checklist Template

Each phase must pass:
- [ ] All code changes applied
- [ ] All tests created and passing
- [ ] Zero regressions — all pre-existing test executables exit 0
- [ ] Build clean — `cmake --build build` completes with no errors and no warnings (`-Werror`)
- [ ] clang-format clean — `git clang-format --diff --quiet` passes
- [ ] clang-tidy clean — no warnings on new/changed code
- [ ] Memory clean — no leaks detected
- [ ] Thread-safe — no mutable globals added; cross-thread data uses `channel<T>`
- [ ] Committed and pushed to git
- [ ] Roadmap updated with phase lessons
- [ ] Second-pass audit performed
- [ ] Web research performed for unclear implementation steps
- [ ] Temp files cleaned
