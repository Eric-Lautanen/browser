# Project Structure

## Root
- `CMakeLists.txt` — Master CMake build (7 static libs + test exes + main browser exe)
- `README.md` — Build prerequisites & instructions (C++20, GCC 15+, CMake 3.20+, Ninja)
- `bookmarks.txt` — Persisted bookmarks data file (pipe-delimited)
- `structure.md` — This file

## src/
- `src/main.cpp` — Entry point: creates BrowserWindow, loads URL from argv, runs event loop

## browser/ — Chrome / UI layer
- `browser/browser_window.cpp` — Main window: event loop, chrome (tabs, URL bar, nav buttons, menu, settings), page rendering, scroll, mouse/keyboard, clipboard, tooltips
- `browser/browser_window.hpp` — Header: BrowserWindow class, ChromeUI layout constants, TabInfo
- `browser/page_loader.cpp` — URL loading pipeline: about/file/HTTP, HTML parse, CSS cascade, layout, paint → LoadedPage
- `browser/page_loader.hpp` — Header: PageLoader, LoadedPage struct
- `browser/telemetry.cpp` — Performance telemetry recorder (page loads, DNS, TLS, paint, GC) + HTML report
- `browser/telemetry.hpp` — Header: Telemetry class, TelemetryEvent
- `browser/history.cpp` — Navigation history: push, go_back, go_forward with capped stack
- `browser/history.hpp` — Header: HistoryManager
- `browser/settings.cpp` — Settings: theme, homepage, search engine; HTML settings page; file save/load
- `browser/settings.hpp` — Header: SettingsManager
- `browser/bookmarks.cpp` — Bookmarks: add/remove/check/save/load from pipe-delimited file
- `browser/bookmarks.hpp` — Header: BookmarkManager, Bookmark struct
- `browser/theme.hpp` — Inline Theme struct with light/dark mode colors + toggle

## html/ — HTML engine
- `html/token.hpp` — Token types: DoctypeToken, TagToken, CommentToken, CharacterToken, EOFToken, Attribute, Token variant
- `html/tokenizer.cpp` — HTML5 tokenizer (DATA/RCDATA/RAWTEXT/SCRIPT states, char refs, CDATA)
- `html/tokenizer.hpp` — Header: Tokenizer class, 60+ state enum
- `html/entities.hpp` — Header: NamedEntity struct, external HTML_ENTITIES table decl
- `html/entities.cpp` — ~2125 HTML named character entity table (e.g. &amp; → &)
- `html/utf8.cpp` — UTF-8 decoder with overlong/surrogate/invalid seq detection
- `html/utf8.hpp` — Header: decode_utf8(), encode_utf8()
- `html/dom.hpp` — DOM types: Node, Document, DocumentType, Element, Text, Comment + factories
- `html/dom.cpp` — DOM utils: Element::id(), class_list(), get_attribute(), etc.
- `html/parser.cpp` — HTML5 tree construction: insertion modes, foster parenting, adoption agency algorithm
- `html/parser.hpp` — Header: Parser class, InsertionMode enum, convenience parse()
- `html/traversal.cpp` — DOM traversal: find_element_by_tag(), inner_text(), serialize_dom()
- `html/traversal.hpp` — Header: traversal functions, traverse_depth_first() template

## css/ — CSS engine
- `css/tokenizer.cpp` — CSS tokenizer: identifiers, numbers, dimensions, strings, URLs, hashes, at-keywords
- `css/tokenizer.hpp` — Header: CssTokenizer, CssToken, CssTokenType enum
- `css/parser.cpp` — CSS parser: stylesheets, rules, selectors, declarations, @media, error recovery
- `css/parser.hpp` — Header: CssParser, parse()
- `css/css_values.cpp` — CSS value types: Color::from_hex(), from_name() (148 named colors), from_rgba()
- `css/css_values.hpp` — Header: Length, Color, CSSValue, SimpleSelector, Selector, Rule, AtRule, StyleSheet
- `css/specificity.hpp` — Inline compute_specificity(): packed 32-bit specificity (IDs/classes/tags)
- `css/selector_match.cpp` — Selector matching: compound/selector match, combinators, pseudo-classes
- `css/selector_match.hpp` — Header: selector matching functions
- `css/cascade.cpp` — CSS cascade: ComputedStyle, origin/specificity sorting, shorthand expansion, inheritance
- `css/cascade.hpp` — Header: MatchedDecl, ComputedStyle, Cascade
- `css/layout.cpp` — Layout engine: block/inline, Flexbox (row/col, wrap, grow/shrink), CSS Grid (fr, gaps, auto-place)
- `css/layout.hpp` — Header: LayoutEngine, LayoutNode, Rect, EdgeSizes, flex enums
- `css/grid.cpp` — Grid utilities: parse_track_list(), parse_grid_line(), track types
- `css/grid.hpp` — Header: GridTrackDef, GridPlacement, GridArea, parsing funcs

## js/ — JavaScript engine
- `js/token.hpp` — JS token types: punctuation, operators, literals, identifiers, template parts
- `js/lexer.cpp` — JS lexer: regexp disambiguation, string/template literals, hex/octal/binary numbers, unicode escapes
- `js/lexer.hpp` — Header: Lexer, lookahead, division-vs-regexp detection
- `js/ast.hpp` — Full AST: expressions (literal, binary, call, arrow, templates), statements, patterns, Program
- `js/parser.cpp` — JS recursive-descent parser: precedence, arrow functions, destructuring, templates
- `js/parser.hpp` — Header: Parser
- `js/bytecode.hpp` — Bytecode ISA: 40+ opcodes, Instruction, BytecodeFunction (consts + child funcs)
- `js/compiler.cpp` — Bytecode compiler: AST walk, scope tracking, break/continue patching, destructuring
- `js/compiler.hpp` — Header: Compiler
- `js/value.hpp` — Runtime values: JSValue tagged union, JSObject, JSFunction (bytecode/native)
- `js/gc.cpp` — Mark-sweep GC: alloc/free, root marking, sweep, threshold-based auto-GC
- `js/gc.hpp` — Header: GCHeap, GCObject, GCJSObject, GCJSFunction
- `js/vm.cpp` — Stack-based VM: call frames, try/catch, built-ins, equality, type coercion, arithmetic
- `js/vm.hpp` — Header: VM, CallFrame
- `js/jit.cpp` — x86-64 JIT: X64Assembler (mov, arithmetic, SIMD, jumps), JITCompiler, ExecutableMemory
- `js/jit.hpp` — Header: X64Assembler, JITCompiler, ExecutableMemory, JITState, extern "C" helpers
- `js/dom_bindings.cpp` — JS DOM bindings: wrap HTML elements, expose getInnerHTML, querySelector, addEventListener, etc.
- `js/dom_bindings.hpp` — Header: DOMBindings, DOMWrapperMap, NativeCallContext

## net/ — Networking
- `net/socket.cpp` — Abstract socket: IPv4Address/IPv6Address parsing/formatting
- `net/socket.hpp` — Header: Socket (TCP), UDPSocket interfaces, IPAddress
- `net/socket_win32.cpp` — Win32 sockets: Win32TCPSocket, Win32UDPSocket (Winsock, refcounted WSA)
- `net/socket_win32.hpp` — Header: Win32 socket classes
- `net/dns.cpp` — DNS resolver: A-record queries, response parsing, domain encoding
- `net/dns.hpp` — Header: DNSResolver
- `net/connection.cpp` — TCP connection: wraps socket + DNS with configurable timeout
- `net/connection.hpp` — Header: Connection, ConnectionConfig
- `net/url.cpp` — URL parser (RFC 3986), url_encode(), url_decode(), relative resolution
- `net/url.hpp` — Header: URL struct
- `net/http.cpp` — HTTP/1.1: Headers, Request::serialize(), Response::parse(), HTTP1Client
- `net/http.hpp` — Header: Method, Status, Headers, Request, Response, HTTP1Client
- `net/http2.cpp` — HTTP/2: frame serialization, HPACK (Huffman + static/dynamic table), multiplexed streams
- `net/http2.hpp` — Header: HTTP/2 frame types, error codes, HPack, HTTP2Client
- `net/http_client.cpp` — High-level HTTP: auto-negotiate HTTP/1.1 vs HTTP/2, ALPN, tracker blocking
- `net/http_client.hpp` — Header: HTTPClient
- `net/tracker_blocker.cpp` — Ad/tracker blocker: default blocklist (~50 domains), URL filtering
- `net/tracker_blocker.hpp` — Header: TrackerBlocker
- `net/deflate.cpp` — Deflate/gzip decompress: BitReader, Huffman trees, inflate, gzip_decompress()
- `net/deflate.hpp` — Header: inflate(), gzip_decompress()
- `net/huffman_table.inc` — HPACK Huffman code table for HTTP/2 header compression
- `net/tls.cpp` — TLS 1.3: ClientHello, ServerHello, X25519 key exchange, AES-GCM/ChaCha20-Poly1305, HKDF
- `net/tls.hpp` — Header: TLSConnection, content/handshake types
- `net/crypto/sha.cpp` — SHA-256 hash
- `net/crypto/sha.hpp` — Header: SHA-256
- `net/crypto/aes.cpp` — AES encryption
- `net/crypto/aes.hpp` — Header: AES
- `net/crypto/chacha20.cpp` — ChaCha20 stream cipher
- `net/crypto/chacha20.hpp` — Header: ChaCha20
- `net/crypto/poly1305.cpp` — Poly1305 MAC
- `net/crypto/poly1305.hpp` — Header: Poly1305
- `net/crypto/bignum.cpp` — Big number arithmetic (for ECC)
- `net/crypto/bignum.hpp` — Header: big number
- `net/crypto/ecc.cpp` — Elliptic curve crypto
- `net/crypto/ecc.hpp` — Header: ECC
- `net/crypto/x25519.cpp` — X25519 Curve25519 ECDH key exchange
- `net/crypto/x25519.hpp` — Header: X25519

## platform/ — Platform abstraction
- `platform/window.hpp` — Abstract Window, Event (kbd/mouse/resize/close), KeyCode enum
- `platform/window.cpp` — Window::create_window() factory → Win32Window
- `platform/window_win32.cpp` — Win32 HWND window: WNDCLASS, proc (msg→events), WGL OpenGL context, custom titlebar
- `platform/window_win32.hpp` — Header: Win32Window
- `platform/opengl.cpp` — load_opengl_functions(): loads GL entry points via wglGetProcAddress
- `platform/opengl.hpp` — Header: OpenGL function pointer declarations

## render/ — Rendering pipeline
- `render/shaders.hpp` — Inline GLSL vertex/fragment shaders for 2D renderer (color + texture)
- `render/shader_program.cpp` — OpenGL shader program: compile/link/bind, uniform setters, cached locations
- `render/shader_program.hpp` — Header: ShaderProgram
- `render/mesh.cpp` — Mesh2D: vertex/index buffers, batch quad/line drawing, GPU upload
- `render/mesh.hpp` — Header: Vertex2D, Mesh2D
- `render/renderer.cpp` — Renderer: fill rects, strokes, lines, textured quads, icons, viewport
- `render/renderer.hpp` — Header: Renderer, Color, Mat4
- `render/texture.cpp` — Texture2D: create/update/bind/unbind
- `render/texture.hpp` — Header: Texture2D
- `render/icons.hpp` — Icon atlas: 10 icons (back, forward, refresh, stop, close, minimize, maximize, menu, bookmark) as 16×16 bitmaps
- `render/font.cpp` — TrueType parser: cmap/loca/glyf/hmtx tables, scanline glyph rasterization, kerning, hinting
- `render/font.hpp` — Header: FontFace, FontManager, GlyphBitmap
- `render/text_renderer.cpp` — Text renderer: glyph atlas (1024×1024 pages), caching, text width measurement
- `render/text_renderer.hpp` — Header: TextRenderer, glyph cache
- `render/embedded_font.cpp` — Embedded default TrueType font binary blob (procedurally generated)
- `render/embedded_font.hpp` — Header: DEFAULT_FONT_DATA, DEFAULT_FONT_DATA_SIZE
- `render/paint.hpp` — Display list: PaintCommand (fill rect, draw text, push/pop clip), css_to_render_color()
- `render/paint.cpp` — DisplayList: push/clear/commands
- `render/painter.cpp` — Painter: walks layout tree, emits display list (backgrounds, borders, text, clipping)
- `render/painter.hpp` — Header: Painter
- `render/paint_executor.cpp` — PaintExecutor: plays DisplayList through Renderer + TextRenderer with offsets/scissors
- `render/paint_executor.hpp` — Header: PaintExecutor

## scripts/
- `scripts/gen_font.py` — Python script that generates embedded_font.cpp (procedural TrueType with ASCII glyph grids)

## tests/ — Test suite (custom framework)
- `tests/utility.hpp` — Universal type aliases (u8..f64) + Result<T,E> template (Rust-style)
- `tests/test_framework.hpp` — TestRegistry, TEST()/ASSERT()/ASSERT_EQ() macros, colored output
- `tests/test_framework.cpp` — Test runner: registration, execution, results reporting
- `tests/main.cpp` — Test entry point: runs all or filtered tests
- `tests/test_framework_test.cpp` — Self-tests for the test framework
- `tests/test_framework_impl_test.cpp` — Implementation tests for test framework
- `tests/utility_test.cpp` — Tests for utility types (Result, etc.)
- `tests/html_test.cpp` — Tests: HTML tokenizer, parser, entities, UTF-8, DOM, serialization
- `tests/parser_test.cpp` — Tests: CSS parser
- `tests/css_test.cpp` — Tests: CSS colors, tokenizer, parser, selectors, cascade, layout
- `tests/font_test.cpp` — Tests: font loading, glyph rasterization
- `tests/grid_test.cpp` — Tests: CSS Grid layout
- `tests/flex_test.cpp` — Tests: CSS Flexbox layout
- `tests/paint_test.cpp` — Tests: paint/element display list generation
- `tests/mesh_test.cpp` — Tests: 2D mesh/vertex operations
- `tests/js_test.cpp` — Tests: JS lexer (numbers, strings, templates, operators)
- `tests/compiler_test.cpp` — Tests: JS bytecode compiler
- `tests/vm_test.cpp` — Tests: JS VM execution
- `tests/gc_test.cpp` — Tests: garbage collector
- `tests/jit_test.cpp` — Tests: JIT compiler
- `tests/dom_bindings_test.cpp` — Tests: JS DOM bindings
- `tests/net_test.cpp` — Tests: URL parsing, DNS, HTTP/1.1, HTTP/2 frames, HPACK
- `tests/tls_test.cpp` — Tests: TLS 1.3 handshake/encryption
- `tests/tracker_test.cpp` — Tests: tracker blocker
- `tests/history_test.cpp` — Tests: browser history manager
- `tests/bookmark_test.cpp` — Tests: bookmark manager
- `tests/settings_test.cpp` — Tests: settings manager
- `tests/telemetry_test.cpp` — Tests: telemetry system
- `tests/chrome_test.cpp` — Tests: browser chrome (theme colors, button hit-testing)
- `tests/gpu_smoke_test.cpp` — GPU smoke test (full OpenGL render pipeline)
- `tests/window_smoke_test.cpp` — Window smoke test (create/destroy platform window)
