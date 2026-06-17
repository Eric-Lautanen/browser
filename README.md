# browser — Web Browser from Scratch in C++20

A from-scratch web browser built in modern C++20 with its own HTML5 tokenizer + tree-construction parser, CSS engine (selector cascade, block/inline/Flexbox/Grid/table layout, animations), JavaScript runtime (ECMAScript lexer → recursive-descent parser → bytecode compiler → stack VM → x86-64 JIT, mark-sweep GC, 14 built-in modules including Promise, fetch, XHR), networking stack (DNS over UDP, TCP via IOCP, TLS 1.3 with X25519/AES-GCM/ChaCha20-Poly1305, HTTP/1.1, HTTP/2 with HPACK + stream multiplexing, WebSocket, deflate/gzip), image decoders (BMP/PNG/GIF/JPEG from scratch), and an OpenGL 3.3 paint engine with TrueType font rasterization — all with zero third-party dependencies.

![current_screenshot](current_screenshot.png)

## Philosophy

**Zero dependencies.** No Boost, Qt, WebKit, Chromium, libcurl, OpenSSL, zlib, Freetype, or any other third-party library. Only the C++ standard library, Win32 API, and OpenGL. Every line of HTML parsing, CSS layout, JavaScript execution, TLS encryption, HTTP/2 multiplexing, font rasterization, and OpenGL rendering is hand-written.

**No telemetry, ever.** The browser tracks its own local performance for debugging only, never phones home, and has no analytics, ads, or user tracking.

**Secure by default.** TLS 1.3 only. Same-origin policy, CSP, CORS, HSTS enforced. Downloads disabled by default with extension blocklists. No passwords stored without encryption.

**Simple & clean.** Minimal settings. No bloat. No exceptions, no RTTI, no STL containers in hot paths. The entire browser is ~55K lines of C++20, compared to Chromium's ~30M lines.

**Built with AI.** This project is developed collaboratively with DeepSeek V4 and other frontier AI models on free tiers — proving what's possible when human intent meets machine assistance, no big-budget team required.

**~55K lines of C++20** across 352 source files (excluding build artifacts).

## Components

### Pipeline

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
                    Compositor → OpenGL 3.3 → Win32 Window
```

### Build

Requires GCC 15.2+, CMake 3.20+, and Ninja.

```bash
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -S . -B build
ninja -C build browser          # Build browser executable
ninja -C build run_tests        # Run all 30+ test executables
```

| Directory | Files | Description |
|-----------|-------|-------------|
| `async/` | 10 (hdr) | C++20 coroutines: task, channel, executor, thread pool, allocators with leak detection |
| `platform/` | 4 cpp | Win32 window + WGL OpenGL context, event loop, audio (waveOut) |
| `image/` | 7 files | Hand-written BMP/PNG/GIF/JPEG decoders (no libpng/libjpeg) |
| `net/` | 39 files | DNS over UDP, TCP via IOCP, TLS 1.3 (X25519, AES-GCM, ChaCha20-Poly1305), HTTP/1.1, HTTP/2 (HPACK, stream mux), WebSocket, deflate/gzip, CSP/HSTS, cookie jar, localStorage, tracker blocker |
| `html/` | 23 files | HTML5 tokenizer (13 insertion modes), tree-construction parser, DOM, preload scanner, resource loader, form handling |
| `css/` | 18 files | Tokenizer, parser, cascade engine (selector match, specificity), layout (block/inline, Flexbox, CSS Grid, table, positioned), animations |
| `js/` | 22 files | ECMAScript lexer, parser (expressions, statements, patterns), bytecode compiler, stack VM (596 loc ops), x86-64 JIT, mark-sweep GC, 14 builtin modules, DOM bindings (document, fetch, XHR, storage) |
| `render/` | 44 files | OpenGL 3.3 renderer, paint display list + executor, TrueType font parser/rasterizer/glyph atlas, compositor + layer tree, Canvas 2D, SVG, form controls, audio/video elements |
| `browser/` | 24 files | Chrome UI (tabs, URL bar, toolbar, titlebar), async page loading pipeline, history, bookmarks, settings, telemetry, download manager, session save/restore |
| `tests/` | ~30 exes | Custom minimal test framework, per-module test suites |
