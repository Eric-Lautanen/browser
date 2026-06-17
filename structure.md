# browser — Web Browser from Scratch

**Lang:** C++20 | **Build:** CMake 3.20+ / Ninja / GCC 15.2+ | **Platform:** Win32 x86-64
**Philosophy:** Zero third-party deps, no telemetry, secure by default
**Totals:** 352 .hpp/.cpp files, ~55K LOC, 30+ test executables

---

## Directory Tree (src)

```
C:\github\browser\
├── .clang-format
├── .gitignore
├── CMakeLists.txt              # 10 lib targets + 30+ test exes + browser exe
├── README.md
├── roadmad.md
│
├── src/
│   └── main.cpp                # Entry: creates BrowserWindow, navigate, event loop
│
├── async/                      # C++20 coroutine infra (header-only INTERFACE lib)
│   ├── task.hpp                # task<T> with Result<T> error propagation
│   ├── channel.hpp             # Lock-free SPSC ring-buffer channel
│   ├── executor.hpp            # Awaiters for thread pool / IOCP dispatch
│   ├── when_all.hpp            # Concurrent task awaiter
│   ├── when_any.hpp            # First-to-complete awaiter
│   ├── mutex.hpp               # SRWLock-based mutex
│   ├── scoped_lock.hpp         # RAII lock guard
│   ├── thread_pool.hpp         # Win32 CreateThreadpool wrapper
│   ├── memory.hpp              # Custom allocator w/ leak detection, per-subsystem tracking
│   └── this_thread.hpp         # set_name(), yield()
│
├── platform/                   # Platform abstraction (Win32)
│   ├── window.hpp / .cpp       # Abstract Window + factory (Win32 → Win32Window)
│   ├── window_win32.hpp/.cpp   # Win32 impl: creation, WGL, event loop, fullscreen (396 loc)
│   ├── opengl.hpp / .cpp       # GL 3.3+ function pointer loading via wglGetProcAddress
│   └── audio.hpp / .cpp        # WAV loading, waveOut* playback (191 loc)
│
├── image/                      # Hand-written image decoders
│   ├── format.hpp / .cpp       # ImageFormat enum, detect_format()
│   ├── decoder.hpp / .cpp      # Abstract Decoder + factory dispatch
│   ├── decoder_bmp.cpp         # BMP: 1/4/8/16/24/32 bpp, RLE8 (207 loc)
│   ├── decoder_png.cpp         # PNG: deflate, filters, palette, RGBA (229 loc)
│   ├── decoder_gif.cpp         # GIF: LZW, palette, interlace (261 loc)
│   └── decoder_jpeg.cpp        # JPEG: Huffman, IDCT, YCbCr upsampling (513 loc)
│
├── net/                        # Full networking stack
│   ├── socket/
│   │   ├── types.hpp / .cpp    # Span, IPv4/6Address, Socket/UDPSocket abstract + factory
│   │   ├── socket_win32.hpp
│   │   ├── tcp.cpp             # Win32 TCP: sync + async via IOCP + ConnectEx (443 loc)
│   │   └── udp.cpp             # Win32 UDP: sync + async via IOCP (128 loc)
│   ├── iocp.hpp / .cpp         # IoOverlapped + IOCP singleton + worker threads (105 loc)
│   ├── dns.hpp / .cpp          # Async DNS A-record via UDP (Google DNS) (151 loc)
│   ├── connection.hpp / .cpp   # Connection: resolve→connect→send/recv (137 loc)
│   ├── url.hpp / .cpp          # RFC 3986 URL parse, resolve, encode/decode (265 loc)
│   ├── origin.hpp / .cpp       # Same-origin policy (43 loc)
│   ├── csp.hpp / .cpp          # Content-Security-Policy parser + checks (219 loc)
│   ├── hsts.hpp / .cpp         # HSTS: header parse, preload list, persistence (198 loc)
│   ├── http.hpp / .cpp         # HTTP/1.1: headers, request/response, chunked (343 loc)
│   ├── http2/
│   │   ├── connection.hpp/.cpp # HTTP/2: preface, settings, stream mux (515 loc)
│   │   ├── frames.cpp          # Frame serialization/deserialization (109 loc)
│   │   ├── hpack.cpp           # HPACK: Huffman + static/dynamic table (396 loc)
│   │   └── internal.hpp        # Big-endian helpers, status reason strings
│   ├── http_client.hpp / .cpp  # Top-level HTTP fetch: HTTP/1.1↔HTTP/2, cookies, CORS (341 loc)
│   ├── tls/
│   │   ├── connection.hpp/.cpp # TLS 1.3 connection state, encrypted send/recv (115 loc)
│   │   ├── record.cpp          # Raw/encrypted record layer (160 loc)
│   │   ├── handshake.cpp       # ClientHello, X25519, cert parse, Finished (629 loc)
│   │   ├── cipher.cpp          # AES-128-GCM + ChaCha20-Poly1305 AEAD (182 loc)
│   │   └── cert_verify.hpp/.cpp# Win32 CryptoAPI cert chain validation (210 loc)
│   ├── websocket.hpp / .cpp    # WS handshake, frame encode/decode, SHA-1 (493 loc)
│   ├── http_cache.hpp / .cpp   # File-backed LRU cache, freshness, revalidation (306 loc)
│   ├── deflate.hpp / .cpp      # Deflate + gzip decompression, Huffman trees (274 loc)
│   ├── crypto/
│   │   ├── chacha20.hpp / .cpp # ChaCha20 stream cipher (67 loc)
│   │   ├── poly1305.hpp / .cpp # Poly1305 MAC (106 loc)
│   │   ├── aes.hpp / .cpp      # AES-128/256, key expansion, GCM (282 loc)
│   │   ├── sha.hpp / .cpp      # SHA-256, SHA-384, HMAC, HKDF (350 loc)
│   │   ├── bignum.hpp / .cpp   # Big integer: mod arith, exp, inv, random (256 loc)
│   │   ├── ecc.hpp / .cpp      # EC: secp256r1, secp384r1 point ops (164 loc)
│   │   └── x25519.hpp / .cpp   # X25519: Montgomery ladder key exchange (260 loc)
│   ├── cookie_jar.hpp / .cpp   # Cookie storage, domain/path match, Netscape file (173 loc)
│   ├── storage.hpp / .cpp      # localStorage/sessionStorage, file-backed (166 loc)
│   ├── tracker_blocker.hpp/.cpp# Domain-based tracker blocking, 40-rule default list (80 loc)
│   └── huffman_table.inc       # Static HPACK Huffman table (257 entries)
│
├── html/                       # HTML5 engine
│   ├── dom.hpp / .cpp          # Node, Element, Document, Text, Comment types (50 loc)
│   ├── token.hpp               # Token variants (TagToken, CharacterToken, etc.)
│   ├── entities.hpp / .cpp     # HTML entity → char mapping (~2200 entries, 2237 loc)
│   ├── utf8.hpp / .cpp         # UTF-8 encode/decode (44 loc)
│   ├── tokenizer/
│   │   ├── tokenizer.hpp / .cpp# 13-mode HTML tokenizer (220 loc)
│   │   ├── states_data.cpp     # Data & char ref states (188 loc)
│   │   ├── states_tag.cpp      # Tag open/close/name states (645 loc)
│   │   ├── states_rcdata.cpp   # RCDATA/RAWTEXT/script states (420 loc)
│   │   └── states_foreign.cpp  # SVG/MathML foreign content (38 loc)
│   ├── parser/
│   │   ├── parser.hpp / .cpp   # Tree construction, async parse (675 loc)
│   │   ├── initial.cpp         # Initial insertion mode (23 loc)
│   │   ├── before_html.cpp     # Before html mode (29 loc)
│   │   ├── head.cpp            # Head mode (185 loc)
│   │   ├── body.cpp            # Body mode — most elements (566 loc)
│   │   ├── frameset.cpp        # Frameset mode (71 loc)
│   │   ├── after_body.cpp      # After body / after after body (52 loc)
│   │   ├── foster_parenting.cpp# Misnested tag handling (61 loc)
│   │   ├── adoption_agency.cpp # Formatting element cleanup (192 loc)
│   │   └── template.cpp        # Template mode stack (106 loc)
│   ├── traversal.hpp / .cpp    # DOM traversal: for_each, find by tag (136 loc)
│   ├── preload_scanner.hpp/.cpp# Token-peeking preload for <img>/<link>/<script> (86 loc)
│   ├── resource_loader.hpp/.cpp# Priority-queue resource fetcher (87 loc)
│   ├── hit_test.hpp / .cpp     # Layout tree hit testing for events (197 loc)
│   ├── form_state.hpp / .cpp   # Form control state tracking (50 loc)
│   └── form_submission.hpp/.cpp# URL-encoded/multipart form encoding (184 loc)
│
├── css/                        # CSS engine
│   ├── tokenizer.hpp / .cpp    # CSS tokenizer: ident, number, string, URL, etc. (315 loc)
│   ├── parser/
│   │   ├── parser.hpp / .cpp   # Ruleset/at-rule/declaration parsing (145 loc)
│   │   ├── selector.cpp        # Compound selectors, combinators, pseudo (142 loc)
│   │   ├── declaration.cpp     # All property values, functions (707 loc)
│   │   ├── at_rule.cpp         # @media, @font-face, @keyframes (154 loc)
│   │   └── property.cpp        # Property-specific helpers (84 loc)
│   ├── cascade/
│   │   ├── engine.hpp / .cpp   # Selector match, specificity, property resolution (750 loc)
│   │   ├── specificity.cpp     # Specificity calculation (20 loc)
│   │   ├── important.cpp       # !important handling (9 loc)
│   │   ├── inheritance.cpp     # Inherited property resolution (10 loc)
│   │   └── media.cpp           # @media query parse + evaluation (119 loc)
│   ├── css_values.hpp / .cpp   # CSSValue, Length, Color, ComputedStyle (203 loc)
│   ├── specificity.hpp         # Specificity struct (38 loc)
│   ├── selector_match.hpp/.cpp # Element-selector matching w/ pseudo-classes (338 loc)
│   ├── layout/
│   │   ├── types.hpp           # Rect, EdgeSizes, FlexConfig, GridTrackDef (137 loc)
│   │   ├── engine.cpp          # Layout tree build, async layout dispatch (393 loc)
│   │   ├── resolve.cpp         # Length resolution, calc()/clamp() (315 loc)
│   │   ├── type_check.cpp      # Block/inline/flex/grid/table detection (83 loc)
│   │   ├── block.cpp           # Block layout: margins, collapsing (278 loc)
│   │   ├── inline.cpp          # Inline: line boxes, text flow (108 loc)
│   │   ├── flex.cpp            # Flexbox: main/cross axis, distribute, wrap (562 loc)
│   │   ├── grid.cpp            # CSS Grid: track sizing, item placement (529 loc)
│   │   ├── table.cpp           # Table: table/row/cell sizing (292 loc)
│   │   └── positioning.cpp     # Static/relative/absolute/fixed/sticky + float (187 loc)
│   └── animation.hpp / .cpp    # CSS animation: keyframes, timeline, tick() (359 loc)
│
├── js/                         # JavaScript engine (ECMAScript)
│   ├── token.hpp               # Token types
│   ├── lexer.hpp / .cpp        # ECMAScript lexer: keywords, regex, templates (584 loc)
│   ├── ast.hpp                 # Full AST: Expr, Stmt, Pattern, Program (208 loc)
│   ├── parser/
│   │   ├── parser.hpp / .cpp   # Entry + async parse (79 loc)
│   │   ├── expression.cpp      # Binary, unary, call, member, arrow, literals (576 loc)
│   │   ├── statement.cpp       # if/while/for/function/return/try (330 loc)
│   │   ├── declaration.cpp     # Declaration stub (4 loc)
│   │   └── pattern.cpp         # Destructuring patterns (101 loc)
│   ├── bytecode.hpp            # Opcodes, Instruction, BytecodeFunction (47 loc)
│   ├── value.hpp               # JSValue, JSObject, JSFunction
│   ├── vm/
│   │   ├── vm.hpp / .cpp       # VM: call frames, stack, execution loop (363 loc)
│   │   ├── ops.cpp             # All bytecode operations (596 loc)
│   │   ├── builtins.cpp        # Builtin registration (64 loc)
│   │   ├── gc.hpp / .cpp       # Mark-sweep GC (129 loc)
│   │   └── compiler/
│   │       ├── compiler.hpp/.cpp # AST→bytecode compiler (194 loc)
│   │       ├── expr.cpp        # Expression bytecode emission (378 loc)
│   │       └── stmt.cpp        # Statement bytecode emission (152 loc)
│   ├── jit.hpp / .cpp          # x86-64 JIT: X64Assembler, ExecutableMemory (516 loc)
│   ├── builtins/               # All built-in JS objects
│   │   ├── builtins.hpp        # Common utilities (76 loc)
│   │   ├── string.cpp          # String.prototype (237 loc)
│   │   ├── array.cpp           # Array.prototype + static (459 loc)
│   │   ├── object.cpp          # Object static methods (147 loc)
│   │   ├── math.cpp            # Math (63 loc)
│   │   ├── number.cpp          # Number (86 loc)
│   │   ├── symbol.cpp          # Symbol (20 loc)
│   │   ├── json.cpp            # JSON.parse/stringify (177 loc)
│   │   ├── date.cpp            # Date (84 loc)
│   │   ├── regexp.cpp          # RegExp stub (33 loc)
│   │   ├── error.cpp           # Error/TypeError/ReferenceError etc. (58 loc)
│   │   ├── console.cpp         # console.log/warn/error/table etc. (119 loc)
│   │   ├── timers.cpp          # setTimeout/setInterval/rAF/queueMicrotask (125 loc)
│   │   ├── promise.cpp         # Promise: then/catch/finally/all/race (182 loc)
│   │   └── performance.cpp     # performance.now() (19 loc)
│   ├── dom_bindings.hpp / .cpp # DOM↔JS bridge, event system (174 loc)
│   ├── dom_bindings/
│   │   ├── document.cpp        # document.* bindings (223 loc)
│   │   ├── fetch.cpp           # fetch() API (162 loc)
│   │   ├── xhr.cpp             # XMLHttpRequest (278 loc)
│   │   └── storage.cpp         # localStorage/sessionStorage (66 loc)
│   ├── script_runner.hpp / .cpp# Inline/async/deferred script execution (57 loc)
│   └── module_loader.hpp/.cpp  # ES module fetch + dependency resolution (65 loc)
│
├── render/                     # OpenGL rendering engine
│   ├── renderer.hpp / .cpp     # Viewport, layer tree traversal, paint dispatch (188 loc)
│   ├── shader_program.hpp/.cpp # GL shader compile/link + uniform setting (112 loc)
│   ├── shaders.hpp             # Inline GLSL vertex/fragment shaders (26 loc)
│   ├── mesh.hpp / .cpp         # VBO/IBO/VAO management, draw calls (144 loc)
│   ├── texture.hpp / .cpp      # GL texture create/upload/bind (67 loc)
│   ├── paint/
│   │   ├── commands.hpp        # DisplayCommand variants (rect/text/image/gradient/shadow)
│   │   ├── painter.hpp / .cpp  # Display list construction (533 loc)
│   │   ├── executor.hpp / .cpp # OpenGL execution of display commands (320 loc)
│   │   ├── gradient.hpp / .cpp # Linear/radial gradient texture generation (63 loc)
│   │   └── shadow.hpp / .cpp   # Shadow blur (4 loc stub)
│   ├── font/
│   │   ├── font.hpp / .cpp     # Font, Glyph, FontFace types + metrics (70 loc)
│   │   ├── truetype.cpp        # TrueType: cmap/head/hhea/hmtx/loca/glyf/kern (463 loc)
│   │   ├── rasterizer.cpp      # Font scan-conversion rasterization (118 loc)
│   │   ├── atlas.hpp / .cpp    # Glyph atlas: packing, caching, texture upload (165 loc)
│   │   ├── embedded.hpp / .cpp # Embedded Open Sans font data (631 loc)
│   │   └── internal.hpp        # TrueType table structs (51 loc)
│   ├── text_renderer.cpp       # Text layout: shaping, wrapping, glyph pos (170 loc)
│   ├── font_loader.hpp / .cpp  # @font-face async loading, TTF/WOFF (121 loc)
│   ├── compositor.hpp / .cpp   # Frame scheduling, layer composition, scroll (241 loc)
│   ├── layer_tree.hpp / .cpp   # Layer tree from layout (131 loc)
│   ├── tile_cache.hpp / .cpp   # Rasterized tile cache, invalidation (66 loc)
│   ├── rasterizer.hpp / .cpp   # Software rasterization fallback (151 loc)
│   ├── canvas.hpp / .cpp       # Canvas 2D: paths, fills, strokes, text (529 loc)
│   ├── canvas_bindings.cpp     # Canvas JS bindings (306 loc)
│   ├── svg_renderer.hpp / .cpp # SVG: rect, circle, path, text (210 loc)
│   ├── mathml_stub.hpp / .cpp  # MathML basic rendering (109 loc)
│   ├── form_controls.hpp/.cpp  # Input/button/checkbox/radio/select (162 loc)
│   ├── audio_element.hpp / .cpp# <audio> element (52 loc)
│   ├── video_element.hpp / .cpp# <video> element stub (30 loc)
│   └── icons.hpp               # Embedded SVG icons for chrome UI (309 loc)
│
├── browser/                    # Browser application
│   ├── browser_window.cpp      # Main window: init, nav, event loop, render, FPS (910 loc)
│   ├── chrome/
│   │   ├── window.hpp / .cpp   # Chrome UI: tabs, URL bar, buttons (477 loc)
│   │   ├── navigator.cpp       # Back/forward/refresh/stop/navigate (101 loc)
│   │   ├── titlebar.cpp        # Custom titlebar with window controls (90 loc)
│   │   ├── toolbar.cpp         # Toolbar with buttons + URL input (123 loc)
│   │   ├── page_view.cpp       # Page view area, scroll handling (241 loc)
│   │   └── event_handler.cpp   # Keyboard shortcuts, click targets, drag (779 loc)
│   ├── page_loader.hpp / .cpp  # Async pipeline: fetch→decompress→parse→layout→paint (645 loc)
│   ├── history.hpp / .cpp      # Navigation back/forward (36 loc)
│   ├── bookmarks.hpp / .cpp    # Bookmarks w/ file persistence (102 loc)
│   ├── settings.hpp / .cpp     # Settings: homepage, search engine, proxy (206 loc)
│   ├── telemetry.hpp / .cpp    # Local perf counters (no phone-home) (223 loc)
│   ├── perf_counter.hpp / .cpp # QPC-based high-res timers (89 loc)
│   ├── download_manager.hpp/.cpp# Downloads, progress, blocklist (168 loc)
│   ├── find_bar.hpp / .cpp     # In-page find (52 loc)
│   ├── devtools.hpp / .cpp     # DevTools stub (15 loc)
│   ├── session.hpp / .cpp      # Session save/restore (tabs, history) (68 loc)
│   └── theme.hpp               # Color theme constants (62 loc)
│
└── tests/                      # Test suite (custom minimal framework)
    ├── utility.hpp             # u8..f64 + Result<T,E>
    ├── test_framework.hpp/.cpp # TEST() / ASSERT() / ASSERT_EQ() macros
    ├── main.cpp                # Test runner entry
    ├── utility_test.cpp        # Result<T> tests
    ├── async_test.cpp          # Coroutine task lifecycle
    ├── channel_test.cpp        # SPSC channel ping-pong
    ├── thread_pool_test.cpp    # 10K work items
    ├── iocp_test.cpp           # IOCP TCP echo concurrency
    ├── net_test.cpp            # HTTP, TLS, DNS, sockets (582 loc)
    ├── tls_test.cpp            # TLS 1.3 handshake
    ├── websocket_test.cpp      # Frame encode/decode
    ├── http_cache_test.cpp     # Store/retrieve/expiration
    ├── tracker_test.cpp        # Tracker matching
    ├── cookie_test.cpp         # Domain/path matching
    ├── storage_test.cpp        # get/set/remove
    ├── html_test.cpp           # HTML parsing (548 loc)
    ├── css_test.cpp            # CSS parser + cascade (540 loc)
    ├── flex_test.cpp           # Flexbox layout (543 loc)
    ├── grid_test.cpp           # CSS Grid layout (363 loc)
    ├── css_animation_test.cpp  # Keyframe resolution (276 loc)
    ├── js_test.cpp             # Lexer/parser/bytecode/VM (555 loc)
    ├── parser_test.cpp         # JS parser (514 loc)
    ├── compiler_test.cpp       # Bytecode compiler (223 loc)
    └── vm_test.cpp             # VM operations (88 loc)
        + 10 more test files (image, gc, jit, font, paint, mesh, gpu, window, 
          chrome, bookmark, history, settings, telemetry, dom_bindings, web_api)
```

---

## Architecture Overview

```
URL → DNS → TCP → TLS 1.3 → HTTP/1.1 or HTTP/2
                                    ↓
                              HTML5 Parser ←── Preload Scanner
                              /     |    \
                             ↓      ↓     ↓
                        CSS Parser  JS  Image Decoders
                             ↓       ↓
                      Cascade Engine  Bytecode Compiler
                             ↓       ↓
                       Layout Engine  VM / JIT
                             ↓
                        Paint System
                             ↓
                    Compositor → OpenGL → Window
```

## CMake Targets

| Library | Files | Deps | Purpose |
|---------|-------|------|---------|
| `async` | 10 (hdr) | none | Coroutines, channels, thread pool, allocators |
| `platform` | 4 .cpp + hdrs | Win32 | Window, OpenGL, audio |
| `image` | 6 .cpp + hdrs | none | BMP/PNG/GIF/JPEG decoders |
| `net` | 39 files | async (hdr-only) | Full networking: DNS→TLS→HTTP/1.1→HTTP/2 |
| `html` | 23 files | async (hdr), net | HTML5 tokenizer, parser, DOM |
| `css` | 18 files | async (hdr), html | CSS tokenizer, parser, cascade, layout |
| `js` | 22 files | async (hdr), html | JS lexer→parser→compiler→VM→JIT→builtins |
| `render` | 44 files | async (hdr), platform, image | OpenGL paint, fonts, compositor, canvas/SVG |
| `browser_lib` | 24 files | all above | Browser chrome, page pipeline, settings |
| `test_framework` | test utility | none | Custom min test framework |
| `browser` (exe) | main.cpp | browser_lib | Final executable |

## Key Design Decisions

- **No exceptions** — errors via `Result<T, E>` throughout
- **No RTTI** — compile-time polymorphism via templates
- **All async via C++20 coroutines** — `task<T>`, `when_all`, `when_any`, IOCP awaiters
- **Custom allocators** — per-subsystem tracking with leak detection
- **Zero third-party dependencies** — every protocol from scratch (TCP, TLS 1.3, HTTP/1.1, HTTP/2, WebSocket, image codecs, crypto primitives, font parsing)
- **No STL containers** — custom `span<T>`, arena allocators
- **Win32-specific** — IOCP, CreateThreadpool, WGL, CryptoAPI, waveOut
