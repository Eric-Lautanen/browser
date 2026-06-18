# browser — Web Browser from Scratch

**Lang:** C++20 | **Build:** CMake 3.20+ / Ninja / GCC 15.2+ | **Platform:** Win32 x86-64
**Philosophy:** Zero third-party deps, no telemetry, secure by default
**Totals:** 340 .hpp/.cpp/.h/.inc files, ~57K LOC (67K with embedded font data), 40 test executables

---

## Directory Tree

```
C:\github\browser\
├── .clang-format
├── .gitignore
├── AGENTS.md
├── CMakeLists.txt              # 10 lib targets + 40 test exes + browser exe
├── README.md
├── SDF_ROADMAP.md              # Signed distance field font rendering plan
├── current_screenshot.png
│
├── src/
│   └── main.cpp                # Entry: BrowserWindow, CLI, event loop (1273 loc)
│
├── async/                      # C++20 coroutine infra (header-only INTERFACE lib, 10 files, 674 LOC)
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
├── platform/                   # Win32 platform layer (8 files, ~1200 LOC)
│   ├── window.hpp / .cpp       # Abstract Window + factory (Win32 → Win32Window)
│   ├── window_win32.hpp/.cpp   # Win32 impl: creation, WGL, event loop, fullscreen
│   ├── opengl.hpp / .cpp       # GL 3.3+ function pointer loading via wglGetProcAddress
│   └── audio.hpp / .cpp        # WAV loading, waveOut* playback
│
├── image/                      # Hand-written image decoders (7 files, ~1300 LOC)
│   ├── format.hpp              # ImageFormat enum, detect_format()
│   ├── decoder.hpp / .cpp      # Abstract Decoder + factory dispatch
│   ├── decoder_bmp.cpp         # BMP: 1/4/8/16/24/32 bpp, RLE8
│   ├── decoder_png.cpp         # PNG: deflate, filters, palette, RGBA
│   ├── decoder_gif.cpp         # GIF: LZW, palette, interlace
│   └── decoder_jpeg.cpp        # JPEG: Huffman, IDCT, YCbCr upsampling
│
├── net/                        # Full networking stack (65 files, ~9300 LOC)
│   ├── socket/                 # Socket abstraction layer
│   │   ├── types.hpp / .cpp    # Span, IPv4/6Address, Socket/UDPSocket abstract + factory
│   │   ├── socket_win32.hpp    # Win32 socket structs
│   │   ├── tcp.cpp             # Win32 TCP: sync + async via IOCP + ConnectEx
│   │   └── udp.cpp             # Win32 UDP: sync + async via IOCP
│   ├── socket.hpp              # Forwarding → socket/types.hpp
│   ├── socket_win32.hpp        # Forwarding → socket/socket_win32.hpp
│   ├── iocp.hpp / .cpp         # IoOverlapped + IOCP singleton + worker threads
│   ├── dns.hpp / .cpp          # Async DNS A-record via UDP (Google DNS)
│   ├── connection.hpp / .cpp   # Connection: resolve→connect→send/recv
│   ├── url.hpp / .cpp          # RFC 3986 URL parse, resolve, encode/decode
│   ├── origin.hpp / .cpp       # Same-origin policy
│   ├── csp.hpp / .cpp          # Content-Security-Policy parser + checks
│   ├── hsts.hpp / .cpp         # HSTS: header parse, preload list, persistence
│   ├── http.hpp / .cpp         # HTTP/1.1: headers, request/response, chunked
│   ├── http2.hpp               # Forwarding → http2/connection.hpp
│   ├── http2/
│   │   ├── connection.hpp/.cpp # HTTP/2: preface, settings, stream mux
│   │   ├── frames.cpp          # Frame serialization/deserialization
│   │   ├── hpack.cpp           # HPACK: Huffman + static/dynamic table
│   │   └── internal.hpp        # Big-endian helpers, status reason strings
│   ├── http_client.hpp / .cpp  # Top-level HTTP fetch: HTTP/1.1↔HTTP/2, cookies, CORS
│   ├── tls.hpp                 # Forwarding → tls/connection.hpp
│   ├── tls/
│   │   ├── connection.hpp/.cpp # TLS 1.3 connection state, encrypted send/recv
│   │   ├── record.cpp          # Raw/encrypted record layer
│   │   ├── handshake.cpp       # ClientHello, X25519, cert parse, Finished
│   │   ├── cipher.cpp          # AES-128-GCM + ChaCha20-Poly1305 AEAD
│   │   └── cert_verify.hpp/.cpp# Win32 CryptoAPI cert chain validation
│   ├── websocket.hpp / .cpp    # WS handshake, frame encode/decode, SHA-1
│   ├── http_cache.hpp / .cpp   # File-backed LRU cache, freshness, revalidation
│   ├── deflate.hpp / .cpp      # Deflate + gzip decompression, Huffman trees
│   ├── cookie_jar.hpp / .cpp   # Cookie storage, domain/path match, Netscape file
│   ├── storage.hpp / .cpp      # localStorage/sessionStorage, file-backed
│   ├── tracker_blocker.hpp/.cpp# Domain-based tracker blocking, 40-rule default list
│   ├── huffman_table.inc       # Static HPACK Huffman table (257 entries)
│   └── crypto/                 # Hand-written crypto primitives
│       ├── chacha20.hpp / .cpp # ChaCha20 stream cipher
│       ├── poly1305.hpp / .cpp # Poly1305 MAC
│       ├── aes.hpp / .cpp      # AES-128/256, key expansion, GCM
│       ├── sha.hpp / .cpp      # SHA-256, SHA-384, HMAC, HKDF
│       ├── bignum.hpp / .cpp   # Big integer: mod arith, exp, inv, random
│       ├── ecc.hpp / .cpp      # EC: secp256r1, secp384r1 point ops
│       └── x25519.hpp / .cpp   # X25519: Montgomery ladder key exchange
│
├── html/                       # HTML5 engine (38 files, ~7600 LOC)
│   ├── dom.hpp / .cpp          # Node, Element, Document, Text, Comment types
│   ├── token.hpp               # Token variants (TagToken, CharacterToken, etc.)
│   ├── entities.hpp / .cpp     # HTML entity → char mapping (~2200 entries)
│   ├── utf8.hpp / .cpp         # UTF-8 encode/decode
│   ├── tokenizer.hpp           # Forwarding → tokenizer/tokenizer.hpp
│   ├── tokenizer/
│   │   ├── tokenizer.hpp / .cpp# 13-mode HTML tokenizer
│   │   ├── states_data.cpp     # Data & char ref states
│   │   ├── states_tag.cpp      # Tag open/close/name states
│   │   ├── states_rcdata.cpp   # RCDATA/RAWTEXT/script states
│   │   └── states_foreign.cpp  # SVG/MathML foreign content
│   ├── parser.hpp              # Forwarding → parser/parser.hpp
│   ├── parser/
│   │   ├── parser.hpp / .cpp   # Tree construction, async parse
│   │   ├── initial.cpp         # Initial insertion mode
│   │   ├── before_html.cpp     # Before html mode
│   │   ├── head.cpp            # Head mode
│   │   ├── body.cpp            # Body mode — most elements
│   │   ├── frameset.cpp        # Frameset mode
│   │   ├── after_body.cpp      # After body / after after body
│   │   ├── foster_parenting.cpp# Misnested tag handling
│   │   ├── adoption_agency.cpp # Formatting element cleanup
│   │   └── template.cpp        # Template mode stack
│   ├── traversal.hpp / .cpp    # DOM traversal: for_each, find by tag
│   ├── preload_scanner.hpp/.cpp# Token-peeking preload for <img>/<link>/<script>
│   ├── resource_loader.hpp/.cpp# Priority-queue resource fetcher
│   ├── hit_test.hpp / .cpp     # Layout tree hit testing for events + text selection
│   ├── form_state.hpp / .cpp   # Form control state tracking
│   └── form_submission.hpp/.cpp# URL-encoded/multipart form encoding
│
├── css/                        # CSS engine (37 files, ~7600 LOC)
│   ├── tokenizer.hpp / .cpp    # CSS tokenizer: ident, number, string, URL, etc.
│   ├── parser.hpp              # Forwarding → parser/parser.hpp
│   ├── parser/
│   │   ├── parser.hpp / .cpp   # Ruleset/at-rule/declaration parsing
│   │   ├── selector.cpp        # Compound selectors, combinators, pseudo
│   │   ├── declaration.cpp     # All property values, functions
│   │   ├── at_rule.cpp         # @media, @font-face(ignored), @keyframes
│   │   └── property.cpp        # Property-specific helpers
│   ├── cascade.hpp             # Forwarding → cascade/engine.hpp
│   ├── cascade/
│   │   ├── engine.hpp / .cpp   # Selector match, specificity, property resolution
│   │   ├── specificity.cpp     # Specificity calculation
│   │   ├── important.cpp       # !important handling
│   │   ├── inheritance.cpp     # Inherited property resolution
│   │   └── media.cpp           # @media query parse + evaluation
│   ├── css_values.hpp / .cpp   # CSSValue, Length, Color, ComputedStyle
│   ├── specificity.hpp         # Specificity struct
│   ├── selector_match.hpp/.cpp # Element-selector matching w/ pseudo-classes
│   ├── grid.hpp / .cpp         # CSS Grid track definition parsing
│   ├── layout.hpp              # Forwarding → layout/engine.cpp declarations
│   ├── layout/
│   │   ├── types.hpp           # Rect, EdgeSizes, FlexConfig, GridTrackDef
│   │   ├── engine.cpp          # Layout tree build, async layout dispatch
│   │   ├── resolve.cpp         # Length resolution, calc()/clamp()
│   │   ├── type_check.cpp      # Block/inline/flex/grid/table detection
│   │   ├── block.cpp           # Block layout: margins, collapsing
│   │   ├── inline.cpp          # Inline: line boxes, text flow, whitespace
│   │   ├── flex.cpp            # Flexbox: main/cross axis, distribute, wrap
│   │   ├── grid.cpp            # CSS Grid: track sizing, item placement
│   │   ├── table.cpp           # Table: table/row/cell sizing
│   │   └── positioning.cpp     # Static/relative/absolute/fixed/sticky + float
│   ├── animation.hpp / .cpp    # CSS animation: keyframes, timeline, tick()
│   └── transition.cpp          # CSS transitions (stub)
│
├── js/                         # JavaScript engine (52 files, ~7900 LOC)
│   ├── token.hpp               # Token types
│   ├── lexer.hpp / .cpp        # ECMAScript lexer: keywords, regex, templates
│   ├── ast.hpp                 # Full AST: Expr, Stmt, Pattern, Program
│   ├── parser.hpp              # Forwarding → parser/parser.hpp
│   ├── parser/
│   │   ├── parser.hpp / .cpp   # Entry + async parse
│   │   ├── expression.cpp      # Binary, unary, call, member, arrow, literals
│   │   ├── statement.cpp       # if/while/for/function/return/try
│   │   ├── declaration.cpp     # Declaration stub
│   │   └── pattern.cpp         # Destructuring patterns
│   ├── bytecode.hpp            # Opcodes, Instruction, BytecodeFunction
│   ├── value.hpp               # JSValue, JSObject, JSFunction
│   ├── compiler.hpp            # Forwarding → vm/compiler/compiler.hpp
│   ├── gc.hpp                  # Forwarding → vm/gc.hpp
│   ├── vm.hpp                  # Forwarding → vm/vm.hpp
│   ├── vm/
│   │   ├── vm.hpp / .cpp       # VM: call frames, stack, execution loop
│   │   ├── ops.cpp             # All bytecode operations
│   │   ├── builtins.cpp        # Builtin registration
│   │   ├── gc.hpp / .cpp       # Mark-sweep GC
│   │   └── compiler/
│   │       ├── compiler.hpp/.cpp # AST→bytecode compiler
│   │       ├── expr.cpp        # Expression bytecode emission
│   │       └── stmt.cpp        # Statement bytecode emission
│   ├── jit.hpp / .cpp          # x86-64 JIT: X64Assembler, ExecutableMemory
│   ├── builtins/               # All built-in JS objects (15 files)
│   │   ├── builtins.hpp        # Common utilities
│   │   ├── string.cpp          # String.prototype
│   │   ├── array.cpp           # Array.prototype + static
│   │   ├── object.cpp          # Object static methods
│   │   ├── math.cpp            # Math
│   │   ├── number.cpp          # Number
│   │   ├── symbol.cpp          # Symbol
│   │   ├── json.cpp            # JSON.parse/stringify
│   │   ├── date.cpp            # Date
│   │   ├── regexp.cpp          # RegExp
│   │   ├── error.cpp           # Error/TypeError/ReferenceError etc.
│   │   ├── console.cpp         # console.log/warn/error/table etc.
│   │   ├── timers.cpp          # setTimeout/setInterval/rAF/queueMicrotask
│   │   ├── promise.cpp         # Promise: then/catch/finally/all/race
│   │   └── performance.cpp     # performance.now()
│   ├── dom_bindings.hpp / .cpp # DOM↔JS bridge, event system
│   ├── dom_bindings/
│   │   ├── document.cpp        # document.* bindings
│   │   ├── fetch.cpp           # fetch() API
│   │   ├── xhr.cpp             # XMLHttpRequest
│   │   └── storage.cpp         # localStorage/sessionStorage
│   ├── script_runner.hpp / .cpp# Inline/async/deferred script execution
│   └── module_loader.hpp/.cpp  # ES module fetch + dependency resolution
│
├── render/                     # OpenGL rendering engine (48 files, ~16K LOC)
│   ├── renderer.hpp / .cpp     # Viewport, batched quad rendering, shader uniforms
│   ├── shader_program.hpp/.cpp # GL shader compile/link + uniform setting
│   ├── shaders.hpp             # Inline GLSL vertex/fragment shaders (with SDF path)
│   ├── mesh.hpp / .cpp         # VBO/IBO/VAO management, batching
│   ├── texture.hpp / .cpp      # GL texture create/upload/bind, GL_UNPACK_ALIGNMENT=1
│   ├── paint/                  # Paint system
│   │   ├── commands.hpp        # DisplayCommand variants (rect/text/image/gradient/shadow)
│   │   ├── painter.hpp / .cpp  # Display list construction from layout tree
│   │   ├── executor.hpp / .cpp # OpenGL execution of display commands
│   │   ├── gradient.hpp / .cpp # Linear/radial gradient texture generation
│   │   └── shadow.hpp / .cpp   # Shadow blur
│   ├── paint.hpp               # Forwarding → paint/commands.hpp
│   ├── painter.hpp             # Forwarding → paint/painter.hpp
│   ├── paint_executor.hpp      # Forwarding → paint/executor.hpp
│   ├── font/                   # Font system (10 files, ~2500 LOC)
│   │   ├── font.hpp / .cpp     # FontFace, FontManager — stripped to cmap/hhea/hmtx/glyf only
│   │   ├── truetype.cpp        # TrueType: cmap/head/hhea/hmtx/loca/glyf/kern parsing
│   │   ├── rasterizer.cpp      # 4x4 supersampled scanline coverage rasterizer + SDF support
│   │   ├── atlas.hpp / .cpp    # Glyph atlas: packing, caching, texture upload, rendering
│   │   ├── embedded.hpp / .cpp # Embedded Open Sans Regular (122KB, 1086 glyphs, upem=2048)
│   │   ├── emoji.hpp           # 30 monochrome 16x16 emoji bitmaps + EMOJI_TOFU sentinel
│   │   └── internal.hpp        # TrueType table structs, GlyphOutline, GlyphMetrics
│   ├── font.hpp                # Forwarding → font/font.hpp
│   ├── embedded_font.hpp       # Forwarding → font/embedded.hpp
│   ├── text_renderer.hpp       # Forwarding → font/atlas.hpp
│   ├── icons.hpp               # Embedded SVG icons for chrome UI
│   ├── canvas.hpp / .cpp       # Canvas 2D: paths, fills, strokes, text
│   ├── canvas_bindings.cpp     # Canvas JS bindings
│   ├── svg_renderer.hpp / .cpp # SVG: rect, circle, path, text
│   ├── mathml_stub.hpp / .cpp  # MathML basic rendering
│   ├── form_controls.hpp/.cpp  # Input/button/checkbox/radio/select
│   ├── audio_element.hpp / .cpp# <audio> element
│   └── video_element.hpp / .cpp# <video> element stub
│
├── browser/                    # Browser application (31 files, ~5900 LOC)
│   ├── browser_window.cpp      # Main window: init, nav, event loop, render, FPS
│   ├── browser_window.hpp      # Forwarding → chrome/window.hpp
│   ├── paths.hpp               # data_dir() path utility
│   ├── chrome/
│   │   ├── window.hpp / .cpp   # Chrome UI: tabs, URL bar, buttons, text selection
│   │   ├── navigator.cpp       # Back/forward/refresh/stop/navigate
│   │   ├── titlebar.cpp        # Custom titlebar with window controls
│   │   ├── toolbar.cpp         # Toolbar with buttons + URL input
│   │   ├── page_view.cpp       # Page view area, scroll handling, selection rendering
│   │   └── event_handler.cpp   # Keyboard shortcuts, click targets, drag, Ctrl+C/V
│   ├── page_loader.hpp / .cpp  # Async pipeline: fetch→decompress→parse→layout→paint
│   ├── history.hpp / .cpp      # Navigation back/forward
│   ├── bookmarks.hpp / .cpp    # Bookmarks w/ file persistence
│   ├── settings.hpp / .cpp     # Settings: homepage, search engine, proxy
│   ├── telemetry.hpp / .cpp    # Local perf counters (no phone-home)
│   ├── perf_counter.hpp / .cpp # QPC-based high-res timers
│   ├── download_manager.hpp/.cpp# Downloads, progress, blocklist
│   ├── find_bar.hpp / .cpp     # In-page find
│   ├── devtools.hpp / .cpp     # DevTools stub (Console, Elements, Network)
│   ├── session.hpp / .cpp      # Session save/restore (tabs, history)
│   └── theme.hpp               # Color theme constants
│
└── tests/                      # Test suite (custom minimal framework, 43 files, ~9100 LOC)
    ├── utility.hpp             # u8..f64 + Result<T,E>
    ├── utility_test.cpp        # Result<T> tests
    ├── test_framework.hpp/.cpp # TEST() / ASSERT() / ASSERT_EQ() macros
    ├── main.cpp                # Test runner entry
    ├── (40 test executables covering all subsystems)
    └── tools/tests/            # 258 test HTML/CSS fixtures + expected JSON outputs
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
                        OpenGL 3.3 → Win32 Window
```

No compositor thread — rendering is single-threaded with direct OpenGL draw calls.

---

## CMake Targets

| Library | Files | Deps | Purpose |
|---------|-------|------|---------|
| `async` | 10 (hdr) | none | Coroutines, channels, thread pool, allocators |
| `platform` | 4 .cpp + hdrs | Win32 | Window, OpenGL, audio |
| `image` | 6 .cpp + hdrs | none | BMP/PNG/GIF/JPEG decoders |
| `net` | 65 files | async (hdr-only) | Full networking: DNS→TLS→HTTP/1.1→HTTP/2 |
| `html` | 38 files | async (hdr), net | HTML5 tokenizer, parser, DOM |
| `css` | 37 files | async (hdr), html | CSS tokenizer, parser, cascade, layout |
| `js` | 52 files | async (hdr), html | JS lexer→parser→compiler→VM→JIT→builtins |
| `render` | 48 files | async (hdr), platform, image | OpenGL paint, fonts, canvas/SVG |
| `browser_lib` | 31 files | all above | Browser chrome, page pipeline, settings |
| `test_framework` | 2 files | none | Custom minimal test framework |

**Executables:** 40 test executables + `browser` = 41 total

---

## Key Design Decisions

- **No exceptions** — errors via `Result<T, E>` throughout
- **No RTTI** — compile-time polymorphism via templates
- **All async via C++20 coroutines** — `task<T>`, `when_all`, `when_any`, IOCP awaiters
- **Custom allocators** — per-subsystem tracking with leak detection
- **Zero third-party dependencies** — every protocol from scratch
- **Single embedded font** — Open Sans Regular (122KB), no system font dependencies
- **Monochrome emoji atlas** — 30 hand-drawn 16×16 bitmaps, TOFU sentinel for missing glyphs
- **No compositor** — single-threaded rendering, no software tile cache
- **Win32-specific** — IOCP, CreateThreadpool, WGL, CryptoAPI, waveOut

---

## Debug Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Shift+F | Toggle FPS overlay |
| Ctrl+Shift+S | Save viewport screenshot to `viewport_screenshot.bmp` |
| Ctrl+Shift+X | Copy all page text to clipboard |
| Ctrl+L | Focus address bar |
| Ctrl+T | New tab |
| Ctrl+R / F5 | Refresh |
| F11 | Toggle fullscreen |
| F12 | DevTools |
