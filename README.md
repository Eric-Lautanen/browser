# browser — Web Browser from Scratch in C++20

A from-scratch web browser built in modern C++20 with its own HTML5 tokenizer + tree-construction parser, CSS engine (selector cascade, block/inline/Flexbox/Grid/table layout, animations + transitions), JavaScript runtime (ECMAScript lexer → recursive-descent parser → bytecode compiler → stack VM → x86-64 JIT, mark-sweep GC, 15 built-in modules including Promise, fetch, XHR), full networking stack (DNS over UDP, TCP via IOCP, TLS 1.3 with X25519/AES-GCM/ChaCha20-Poly1305, HTTP/1.1, HTTP/2 with HPACK + stream multiplexing, WebSocket, deflate/gzip), image decoders (BMP/PNG/GIF/JPEG from scratch), and an OpenGL 3.3 paint engine with TrueType font rasterization + shaping — all with zero third-party dependencies.

![current_screenshot](current_screenshot.png)

## Philosophy

**Zero dependencies.** No Boost, Qt, WebKit, Chromium, libcurl, OpenSSL, zlib, Freetype, or any other third-party library. Only the C++ standard library, Win32 API, and OpenGL. Every line of HTML parsing, CSS layout, JavaScript execution, TLS encryption, HTTP/2 multiplexing, font rasterization, and OpenGL rendering is hand-written.

**No telemetry, ever.** The browser tracks its own local performance for debugging only, never phones home, and has no analytics, ads, or user tracking.

**Secure by default.** TLS 1.3 only. Same-origin policy, CSP, CORS, HSTS enforced. Downloads disabled by default with extension blocklists. No passwords stored without encryption.

**Simple & clean.** Minimal settings. No bloat. No exceptions, no RTTI, no STL containers in hot paths. The entire browser is ~62K lines of C++20 across 357 source files — compared to Chromium's ~30M lines.

**Built with AI.** This project is developed collaboratively with AI models on free tiers — proving what's possible when human intent meets machine assistance, no big-budget team required.

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
ninja -C build run_tests        # Run all 40 test executables
```

| Directory | Files | LOC | Description |
|-----------|-------|-----|-------------|
| `async/` | 10 (hdr) | 674 | C++20 coroutines: task, channel, executor, thread pool, allocators with leak detection |
| `platform/` | 8 | 1,203 | Win32 window + WGL OpenGL context, event loop, audio (waveOut) |
| `image/` | 7 | 1,274 | Hand-written BMP/PNG/GIF/JPEG decoders (no libpng/libjpeg) |
| `net/` | 65 | 9,087 | DNS over UDP, TCP via IOCP, TLS 1.3 (X25519, AES-GCM, ChaCha20-Poly1305), HTTP/1.1, HTTP/2 (HPACK, stream mux), WebSocket, deflate/gzip, CSP/HSTS, cookie jar, localStorage, tracker blocker, crypto primitives |
| `html/` | 38 | 7,567 | HTML5 tokenizer (13 insertion modes), tree-construction parser, DOM, preload scanner, resource loader, form handling |
| `css/` | 37 | 7,566 | Tokenizer, parser, cascade engine (selector match, specificity), grid track defs, layout (block/inline, Flexbox, CSS Grid, table, positioned), animations + transitions |
| `js/` | 53 | 7,919 | ECMAScript lexer, parser (expressions, statements, patterns), bytecode compiler, stack VM, x86-64 JIT, mark-sweep GC, 15 builtin modules, DOM bindings (document, fetch, XHR, storage), module loader |
| `render/` | 64 | 10,023 | OpenGL 3.3 renderer, paint display list + executor, TrueType font parser/rasterizer/glyph atlas + shaper + registry, compositor + layer tree, Canvas 2D, SVG, form controls, audio/video elements |
| `browser/` | 31 | 6,001 | Chrome UI (tabs, URL bar, toolbar, titlebar), async page loading pipeline, history, bookmarks, settings, telemetry, download manager, session save/restore, devtools |
| `tests/` | 43 | 9,180 | Custom minimal test framework, 40 test executables across all subsystems |
| `src/` | 1 | 1,285 | Program entry point, CLI test runner harness, JSON emitter for test fixtures |
