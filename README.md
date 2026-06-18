# browser — Web Browser from Scratch in C++20

A from-scratch web browser built in modern C++20: HTML5 parser, CSS engine (cascade, Flexbox, Grid, table, animations), JavaScript runtime (bytecode VM + x86-64 JIT, mark-sweep GC, 15 built-in modules including Promise/fetch/XHR), full networking stack (DNS over UDP, TCP via IOCP, TLS 1.3 with X25519/AES-GCM/ChaCha20-Poly1305, HTTP/1.1, HTTP/2 with HPACK, WebSocket), image decoders (BMP/PNG/GIF/JPEG), and OpenGL 3.3 paint engine — all with zero third-party dependencies.

![current_screenshot](current_screenshot.png)

## Philosophy

**Zero dependencies.** No Boost, Qt, WebKit, Chromium, libcurl, OpenSSL, zlib, FreeType, or any other third-party library. Only the C++ standard library, Win32 API, and OpenGL.

**Single embedded font.** Open Sans Regular (122KB) compiled into the binary. No system font enumeration, no `@font-face` HTTP loading, no font fallback chains. Text renders via a single embedded TrueType face.

**No compositor.** Single-threaded rendering — no separate compositor thread, no software tile cache, no layer tree. OpenGL draws directly.

**No telemetry, ever.** Local performance counters only. Never phones home.

**Secure by default.** TLS 1.3 only. Same-origin policy, CSP, CORS, HSTS enforced.

**Built with AI.** Developed collaboratively with AI models — proving what's possible when human intent meets machine assistance.

## Pipeline

```
URL → DNS (UDP, Google) → TCP (IOCP) → TLS 1.3 → HTTP/1.1 or HTTP/2
                                    ↓
                              HTML5 Parser ←── Preload Scanner
                              /     |    \
                             ↓      ↓     ↓
                        CSS Parser  JS  Image Decoders (BMP/PNG/GIF/JPEG)
                             ↓       ↓
                      Cascade Engine  Bytecode Compiler
                             ↓       ↓
                        Layout Engine  VM / JIT (x86-64)
                             ↓
                         Paint System
                             ↓
                         OpenGL 3.3 → Win32 Window
```

## Build

Requires GCC 15.2+, CMake 3.20+, and Ninja.

```bash
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -S . -B build
ninja -C build browser          # Build browser executable
ninja -C build run_tests        # Run all 40 test executables
```

## Components

| Directory | Files | LOC | Description |
|-----------|-------|-----|-------------|
| `async/` | 10 (hdr) | 674 | C++20 coroutines: task, channel, executor, thread pool, allocators |
| `platform/` | 8 | 1,206 | Win32 window + WGL, event loop, audio (waveOut) |
| `image/` | 7 | 1,274 | Hand-written BMP/PNG/GIF/JPEG decoders |
| `net/` | 65 | 9,344 | DNS over UDP, TCP via IOCP, TLS 1.3 (X25519, AES-GCM, ChaCha20-Poly1305), HTTP/1.1, HTTP/2 (HPACK, stream mux), WebSocket, deflate/gzip, CSP/HSTS, cookies, localStorage, tracker blocking, crypto primitives |
| `html/` | 38 | 7,569 | HTML5 tokenizer (13 insertion modes), tree-construction parser, DOM, preload scanner, resource loader, hit testing |
| `css/` | 37 | 7,568 | Tokenizer, parser, cascade engine, layout (block/inline, Flexbox, CSS Grid, table, positioned), animations |
| `js/` | 52 | 7,919 | ECMAScript lexer, parser, bytecode compiler, stack VM, x86-64 JIT, mark-sweep GC, 15 built-in modules, DOM bindings, module loader |
| `render/` | 48 | 16,180 | OpenGL 3.3 renderer, batch-quad mesh, paint display list + executor, TrueType font parser + 4×4 coverage rasterizer + glyph atlas, monochrome emoji atlas, Canvas 2D, SVG, form controls, audio/video elements |
| `browser/` | 31 | 5,930 | Chrome UI (tabs, address bar, toolbar, titlebar, menus), async page loading, history, bookmarks, settings, telemetry, download manager, session save/restore, devtools |
| `tests/` | 43 | 9,062 | Custom minimal test framework, 40 test executables across all subsystems |
| `src/` | 1 | 1,273 | Program entry point, CLI test runner, JSON fixture emitter |

**Total: 340 files, ~57K lines of C++20** (67K including 10K-line embedded font binary data)
