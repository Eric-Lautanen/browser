# Browser Engine Audit — Completion Gaps

**Goal:** Fully complete and usable browser in pure C++20, zero third-party dependencies, all written from scratch. Must load and render modern websites including google.com.

**Current state (2026-08-29):** google.com **loads and renders end-to-end**: DNS → TLS 1.3 → HTTP/2 (ALPN h2) → gzip → HTML5 parse → CSS cascade → layout → paint. The logo image, search box, buttons, and centered footer all render correctly. As of commit `8a0b2b6`, 90/90 harness tests pass and all 42 C++ test executables pass. Remaining gaps are quality gaps (below), not "page won't load" gaps.

---

## ✅ RegExp engine round — COMPLETED & COMMITTED (2026-08-29)

The regex engine that was half-landed is now **finished, debugged, and committed**. All 5 root-cause bugs found during the round are fixed and verified.

### Files in this round

- **NEW `js/regex_engine.hpp` / `js/regex_engine.cpp`** — real backtracking regex engine (recursive-descent parser → instruction program CHAR/ANY/CLASS/SPLIT/JMP/SAVE/BOL/EOL/WORDB/BACKREF/LOOK/MATCH → backtracking matcher, 1M step budget, 4000 depth cap). Supports literals, `\d \D \w \W \s \S` (incl. inside classes, complemented correctly), classes with ranges/negation, anchors `^ $ \b \B` (/m), greedy/lazy quantifiers `* + ? {n,m}` (single-char loops iterate to avoid stack overflow on long inputs), capturing + non-capturing groups, alternation, backreferences, lookahead `(?=)`/`(?!)`, lookbehind `(?<=)`/`(?<!)` (scans candidate starts), ASCII case fold for /i, /s dot-all, /y sticky; /u accepted.
- **`js/builtins/regexp.cpp`** — `new RegExp(pattern, flags)`, `exec`/`test` with g/y lastIndex semantics, `toString`, per-flag probes. Shared helpers in `builtins.hpp`: `value_is_regexp()`, `regexp_exec_at()`, `regexp_make()`, `regexp_expand_replacement()`.
- **`js/builtins/string.cpp`** — `match`/`replace`/`replaceAll`/`search`/`split` all accept RegExp objects.
- **`CMakeLists.txt`** — `js/regex_engine.cpp` added to the js target.
- **`PUSH_REGEX` opcode** (`js/bytecode.hpp`, `js/vm/compiler/expr.cpp`, `js/vm/vm.cpp`) — `/pattern/flags` literal builds the object through the global RegExp constructor.

### Bugs fixed in this round

1. **PUSH_REGEX arg-shift** (`js/vm/vm.cpp`) — the handler passed `{undefined, pattern, flags}` to `invoke()` which *also* prepends `this`, shifting args by one; the constructor saw `source="undefined"` and produced an object with no prototype/`test`. Fix: pass only `{pattern, flags}`.
2. **Null StringCtx crash** (`js/builtins/string.cpp`) — `string_match`/`search`/`replace`/`replaceAll` were registered without the `ctx` param (default `nullptr`); calling them with a regex did `static_cast<StringCtx*>(context)` → null deref SEGFAULT. Fix: pass `ctx` to all four `make_fn` registrations (matches `split`).
3. **IIFE parser failure** (`js/parser/expression.cpp`) — in the `LPAREN` case, a leading `IDENTIFIER` "function" was misread as an identifier by `parse_pattern_or_ident`, making the function-expression branch unreachable, so `(function(){…})()` failed to parse. Fix: detect `function` keyword inside parens and parse the function expression directly.
4. **Lookaround capture-vector assert** (`js/regex_engine.cpp` `run_look`) — `RegexMatch tmp` was created with empty `cap_start`/`cap_end`; the MATCH case wrote `tmp.cap_start[0]` → `vector::_M_range_check`. Fix: size `tmp.cap_start`/`cap_end` to `sub.nslots/2` in both the lookbehind and lookahead branches.
5. **Lazy quantifier not honored** (`js/regex_engine.cpp` `simple_loop`) — the SPLIT loop always started at `end_pos` and stepped back (always greedy), so `<.*?>` matched greedily. Fix: when `inst.lazy_loop`, iterate forward from `sp` trying 0,1,2,… reps.

### Verification

- `tools/run_tests.ps1 -full` → **90/90 PASS, 0 Critical**.
- All **42** `build\*_test.exe` exit 0 (0 failures).
- `ninja -C build browser` clean under `-Wall -Wextra -Wpedantic -Werror`; `git clang-format --diff HEAD` clean on the 5 changed files.
- RegExp verification page (`%TEMP%\rtest.html`, 26 checks) → **25/26 pass**. The only mismatch is `unicode-cp-BAD`: the page asserts `/h.llo/u.test("h\u00e9llo") === false`, but per ES6 `/u` semantics `.` matches the BMP codepoint U+00E9, so the engine correctly returns `true`. This is a wrong **test expectation**, not an engine bug — left as known mismatch.
- Confirmed working: literal `re.test()` (source + prototype present), `new RegExp("ab+c")`, global `match`, lookaround (`(?=)`/`(?!)`, lookbehind approximated), lazy `.*?`, IIFE execution, `replace` with `$n` + function replacements.

### Known limitations intentionally left

- Quantified EMPTY bodies (`(…)?`) guarded only by the step budget; named groups, `$<name>` replacements, and full `/u` astral-codepoint (surrogate-pair) semantics are unsupported.

---

## What Was Fixed to Make google.com Load (this session)

1. **HTTP/2 header casing** — request header names are now lowercased (RFC 7540 §8.1.2); connection-specific headers (Host/Connection/Keep-Alive/…) are stripped from h2 requests.
2. **HTTP/2 PADDED/PRIORITY frames** — DATA/HEADERS padding is stripped per RFC 7540 §6.1/§6.2 (uncommitted work finished and kept).
3. **Resource-queue race** — `fetch_css_content()` drained the whole resource queue, swallowing image bytes into the stylesheet text (the google logo PNG was being appended to merged CSS). It now drains CSS-priority requests only (`ResourceLoader::fetch_all_parallel(ResourcePriority)` overload).
4. **Relative-src images never rendered** — paint looked images up by the raw `src` attribute while the loader keyed them by resolved URL. `html::Element::resolved_src` now carries the absolute URL for paint and layout.
5. **Replaced-element layout** — `<img>`/`<svg>`/`<video>`/`<canvas>` had no intrinsic sizing (0×0 boxes). New `size_replaced_element()` implements CSS 2.1 §10.3.2 (CSS size > width/height attributes > intrinsic size, aspect ratio preserved) with decoded-image sizes fed via `LayoutEngine::set_image_sizes()`.
6. **Inline line-box layout** — text inside inline elements used to inherit the container's full width (footer links stacked). Rebuilt `layout_children`'s inline path: shrink-to-fit natural widths, greedy line breaking, `text-align` line centering, `<br>` handling, inline-block shrink-to-fit, float shrink-to-fit.
7. **Presentational attributes** — width/height/align/valign/nowrap/bgcolor HTML attributes now map to zero-specificity author-origin CSS hints (`collect_presentational_hints`).
8. **UA stylesheet** — added `center{text-align:center}`, replaced elements default `inline-block`, `br{display:inline}`, `input[type=hidden]{display:none}`.
9. **Table layout** — `table-cell` no longer recurses into the table formatter (cells collapsed to 5px); explicit column widths (CSS/attr) are honored; leftover space goes to auto columns.
10. **Renderer texture-state bug** — `draw_textured_quad()` never updated `shader_mode_`, so fills drawn after an image sampled the image texture (black bands + red garbage on google.com). Fixed; right floats also now stack leftward using `float_right_x` (they used to overlap), and `clear` on floats is honored before placement.
11. **Cascade always runs** — pages with no author CSS skipped the cascade entirely, so layout dropped every element (blank pages for file:// and simple pages).
12. **`native_append_child` use-after-free** — the uncommitted stub-detach destroyed the node before re-homing it; `detach_from_parent()` now returns ownership.
13. **Percent widths** — `layout_block` accepts percentage widths (table cells, blocks); margin:auto centering applies to them.

### Debug tooling added

- `browser --screenshot <url> <out.bmp>` renders **remote URLs** headlessly (was file-only) — the main repro loop for site work.
- `BROWSER_NET_DEBUG=1` — verbose fetch/TLS/h2/pipeline tracing (fprintf noise is gated behind it).
- `BROWSER_DUMP_DL=<path>` (with `--screenshot`) — dumps the page display list from the GUI pipeline.

### Fixture workflow warning

`git clang-format` reformats JSON (clang-format ≥17) and engine-bootstrapped fixtures are compared **byte-for-byte** — regenerating a fixture after formatting changes breaks it. `tools/tests/.clang-format` (`DisableFormat: true`) now exempts the fixtures directory. Order of operations: format source first, regenerate fixtures last.

---

## Remaining Gaps Observed on google.com (priority order)

1. **Header (`#gb`) content renders but is mispositioned** — after the flex-item measurement and var() fixes, the Sign-in button and the apps SVG icon render, but the nested flex/float/inline measurement is unstable (`flex-basis:auto` items are measured by a throwaway layout whose stretched states leak into later passes). Needs a real max-content/intrinsic-width measurement that doesn't mutate the tree (CSS sizing §9 intrinsic contributions).
2. **CSS custom properties** — resolution and inheritance work now (including nested fallbacks); still missing: var() inside shorthands that expand positionally (`background`/`animation` slots), `@property` registration, and cycle detection beyond the 64-iteration guard.
3. **RegExp** — a real engine **landed and committed** (2026-08-29; see the completed round section at the top). The previously-shipped state was literal substring matching. Remaining gaps: named groups, `$<name>` replacements, `/u` astral codepoints.
4. **JS API completeness** — DONE since the last audit pass: `createElement`/`createTextNode`/`removeChild` (orphan-ownership model), real CSS-selector `querySelector`/`querySelectorAll`, `textContent` getter/`setTextContent` setter, `document.body/documentElement/title`, `window.location` (href read + assign/replace/reload via a loader callback), `window.navigator`, and **`Map`/`Set`** builtins (SameValueZero keys, insertion order, iterable constructors). Scripts now also **execute on http(s) pages** (inline before layout, deferred/external after fetch) — previously only file:// pages ran JS. Two VM bugs fixed on the way: top-level `try/catch` restarted execution at instruction 0 (unpatched normal-completion jump), and parenthesized ternaries `(x === 1 ? a : b)` silently failed to parse. Still missing: real `Promise` chain semantics, ES modules, `location.href=` setter interception (needs accessor properties on JSObject).
5. **Brotli** — `Accept-Encoding` advertises gzip/deflate only; many CDNs serve `br` to modern UAs (we ask for what we support, so this is an efficiency gap, not a correctness one).

---

## Subsystem Audit

### 1. Networking (net/) — 66 files, ~9500 LOC

| Component | Status | Gap |
|-----------|--------|-----|
| DNS resolver (dns.hpp/cpp) | ✅ Working | Async UDP resolver via Google DNS 8.8.8.8. Includes process-wide cache. |
| Socket layer (socket/) | ✅ Working | Win32 TCP/UDP sync+async via IOCP. |
| IOCP (iocp.hpp/cpp) | ✅ Working | Overlapped I/O with worker threads. |
| TCP connection (connection.hpp/cpp) | ✅ Working | Sync and async paths both present. |
| HTTP/1.1 (http.cpp) | ✅ Working | Request serialization, response parsing (chunked/content-length/close-delimited), keep-alive. |
| HTTP/2 (http2/connection.cpp) | ✅ Working | Preface, settings, HPACK, stream mux, flow control, window updates. |
| HTTP client (http_client.cpp) | ✅ Working | Connect → TLS → ALPN → HTTP/1.1 or HTTP/2. Cookie jar, CORS origin header. |
| TLS 1.3 (tls/) | ⚠️ Partial | Handshake complete (ClientHello → Finished). X25519 key exchange, AES-128-GCM/ChaCha20-Poly1305, HKDF, transcript hashing. **Certificate verification uses Win32 CryptoAPI** which may fail on systems without proper root store. Certificate parsing supports X.509 DER. |
| HSTS (hsts.hpp/cpp) | ✅ Working | Header parse, preload list, persistence. |
| CSP (csp.hpp/cpp) | ✅ Working | Parser + checks for script-src, style-src, default-src, img-src. |
| URL parser (url.hpp/cpp) | ✅ Working | RFC 3986 parse, resolve, encode/decode. |
| Cookie jar (cookie_jar.hpp/cpp) | ✅ Working | Domain/path match, Netscape file format. |
| Tracker blocker (tracker_blocker.hpp/cpp) | ✅ Working | 40-rule default list. |
| HTTP cache (http_cache.hpp) | ⚠️ Stub | Header exists but `init()`/`lookup()`/`store()` not implemented. File-backed LRU cache is declared but likely empty. |
| WebSocket (websocket.hpp/cpp) | ⚠️ Partial | Frame encode/decode, SHA-1, handshake exist. May not be wired into the page loader or fetch. |
| Storage (storage.hpp/cpp) | ⚠️ Partial | localStorage/sessionStorage declared, file-backed persistence exists but may not be wired to the JS DOM bindings. |
| Deflate/gzip (deflate.hpp/cpp) | ✅ Working | Decompression for transfer-encoding. |
| Origin (origin.hpp/cpp) | ✅ Working | Same-origin policy. |

**Networking gaps:**
- No network diagnostic logging — impossible to debug why google.com fails
- HTTP cache is declared but likely not populated/used
- WebSocket not integrated into page load pipeline
- Storage not wired to DOM bindings
- No HTTP/1.1 pipelining
- No proxy support (settings.hpp declares it but proxy config is not applied to connections)
- No QUIC/UDP-based HTTP/3

### 2. HTML Engine (html/) — 38 files, ~7600 LOC

| Component | Status | Gap |
|-----------|--------|-----|
| Tokenizer (tokenizer/) | ✅ Working | 13-mode tokenizer (data, RCData, RAWTEXT, script, style, plain text, foreign). |
| Tree builder (parser/) | ✅ Working | Initial, before-html, head, body, frameset, after-body, foster parenting, adoption agency, template. |
| DOM (dom.hpp/cpp) | ✅ Working | Node, Element, Document, Text, Comment types with traversal. |
| Preload scanner (preload_scanner.hpp/cpp) | ✅ Working | Token-peeking preload for img/link/script. |
| Resource loader (resource_loader.cpp) | ✅ Working | Priority-queue fetcher with bounded parallel concurrency. |
| Form state (form_state.hpp/cpp) | ✅ Working | Form control state tracking. |
| Form submission (form_submission.hpp/cpp) | ✅ Working | URL-encoded/multipart encoding. |
| Hit test (hit_test.hpp/cpp) | ✅ Working | Layout tree hit testing for events + text selection. |
| Entities (entities.hpp/cpp) | ✅ Working | ~2200 HTML entity mappings. |
| UTF-8 (utf8.hpp/cpp) | ✅ Working | Encode/decode. |

**HTML gaps:**
- Template element content extraction may be incomplete
- No `<dialog>` element support
- No `contenteditable` implementation
- No `designMode` support

### 3. CSS Engine (css/) — 37 files, ~9700 LOC

| Component | Status | Gap |
|-----------|--------|-----|
| Tokenizer (tokenizer.hpp/cpp) | ✅ Working | Ident, number, string, URL, function tokens. |
| Parser (parser/) | ✅ Working | Ruleset, at-rule, declaration parsing. |
| Selector matching (selector_match.hpp/cpp) | ✅ Working | Compound selectors, combinators, pseudo-classes. |
| Cascade engine (cascade/) | ✅ Working | Specificity, !important, inheritance, media queries. |
| Layout (layout/) | ✅ Working | Block, inline, flex, grid, table, positioning, float, multi-column. |
| Grid (grid.hpp/cpp) | ✅ Working | Track definition parsing, item placement. |
| Animation (animation.hpp/cpp) | ✅ Working | Keyframes, timeline, tick, TransitionManager. |
| CSS values (css_values.hpp/cpp) | ✅ Working | Length, Color, ComputedStyle, FontFaceRule, calc()/clamp(). |
| Specificity (specificity.hpp/cpp) | ✅ Working | Specificity calculation. |

**CSS gaps:**
- CSS Grid subgrid not implemented
- CSS container queries not implemented
- CSS nesting not fully implemented (parser supports @nest but cascade may not resolve it)
- CSS :has() may work but not fully tested against all selector combinations
- CSS scroll-driven animations not implemented
- CSS view-transitions not implemented
- CSS painting order (z-index stacking contexts) may have edge cases
- CSS clip-path not implemented
- CSS mask not implemented
- CSS filter chaining may have issues (renderer supports basic filters)
- CSS scrollbar styling not implemented
- CSS logical properties partially implemented

### 4. JavaScript Engine (js/) — 53 files, ~8800 LOC

| Component | Status | Gap |
|-----------|--------|-----|
| Lexer (lexer.hpp/cpp) | ✅ Working | ECMAScript lexer: keywords, regex, templates. |
| Parser (parser/) | ✅ Working | Expression, statement, declaration (stub for import/export/class), pattern. |
| AST (ast.hpp) | ✅ Working | Full AST: Expr, Stmt, Pattern, Program. |
| Bytecode compiler (vm/compiler/) | ✅ Working | AST→bytecode compiler with expression and statement emission. |
| VM (vm/vm.hpp/cpp) | ✅ Working | Call frames, stack, execution loop, VM::invoke(). |
| GC (vm/gc.hpp/cpp) | ✅ Working | Mark-sweep GC, traces function property maps. |
| JIT (jit.hpp/cpp) | ✅ Working | x86-64 JIT with X64Assembler, ExecutableMemory. |
| Builtins (builtins/) | ⚠️ Partial | String, Array, Object, Math, Number, Symbol, JSON, Date, RegExp, Error, console, timers, Promise, performance. |
| DOM bindings (dom_bindings/) | ⚠️ Partial | document, fetch, xhr, storage bindings exist. |
| Script runner (script_runner.cpp) | ✅ Working | Inline/deferred/external script execution. |
| Module loader (module_loader.hpp/cpp) | ❌ Stub | Exists but **"not yet wired into script_runner"**. |

**JS gaps:**
- **RegExp** — a real backtracking engine landed (2026-08-29): `js/regex_engine.cpp` + `regexp.cpp` bindings cover literals/classes/anchors/greedy+lazy quantifiers/capturing+non-capturing groups/alternation/backrefs/lookaround/flags. Known gaps: named groups, `$<name>` replacements, full `/u` astral (surrogate-pair) codepoints; quantified-empty bodies rely on the step budget.
- **Import/export statements not implemented** (declaration.cpp is a stub)
- **Class syntax not fully implemented** (declaration.cpp is a stub)
- **Module loader not wired** — ES modules (`import`/`export`) don't execute
- **Proxy not implemented**
- **Reflect not implemented**
- **BigInt not implemented** (only bignum crypto primitives exist)
- **Iterator/Generator not implemented** — `for...of`, `yield`, spread operator may not work
- **Map/Set not implemented** — these are used by many modern websites
- **WeakMap/WeakSet not implemented**
- **Promise variants** — `Promise.withResolvers()` not implemented
- **Array.from**, **Array.prototype.flat**, **Array.prototype.flatMap** may be missing
- **String.prototype.replaceAll**, **String.prototype.at**, **String.prototype.isWellFormed** may be missing
- **Object.hasOwn**, **Object.groupBy**, **Map.groupBy** not implemented
- **globalThis** may not be properly set
- **structuredClone** not implemented
- **queueMicrotask** may not fully work with the microtask queue
- **FinalizationRegistry** not implemented
- **Atomics** not implemented
- **SharedArrayBuffer** not implemented
- **WebAssembly** not implemented
- **Tail call optimization** not implemented
- **Temporal API** not implemented
- **Records and Tuples** not implemented
- **Decorators** not implemented
- **Import attributes** not implemented
- **JS error.stack** is hardcoded, not a real stack trace
- **`new.target`** not implemented
- **`arguments`** object may not work correctly
- **`this` in arrow functions** may not be lexically bound correctly in all cases
- **Destructuring** in function parameters may be incomplete
- **Default parameters** may not fully work
- **Rest/spread** in function parameters may be incomplete

### 5. DOM Bindings (js/dom_bindings/) — ⚠️ Partial

| Component | Status | Gap |
|-----------|--------|-----|
| Document | ⚠️ Partial | getElementById works. Missing: querySelectorAll, getElementsByClassName, getElementsByTagName, createElement, createTextNode, createDocumentFragment. |
| Fetch | ⚠️ Partial | fetch() implemented but returns a plain promise object, not a real `Response` subclass with `Response.ok`, `Response.json()`, `Response.text()` as proper methods. |
| XHR | ⚠️ Partial | XMLHttpRequest exists but may not be fully wired. |
| Event system | ⚠️ Partial | addEventListener/fireEvent exist but `Event` object creation, bubbling, cancelation are stubbed. `native_append_child` returns undefined — **does not actually modify the DOM**. |
| Element methods | ⚠️ Partial | getAttribute/setAttribute/querySelector work. Missing: appendChild (stubbed), removeChild, insertBefore, cloneNode, getBoundingClientRect. |
| Window | ⚠️ Partial | Minimal window object. Missing: document, location, navigator, screen, history, innerWidth/Height, addEventListener. |
| Location | ❌ Missing | `window.location` is not implemented. Navigation via `location.href` or `location.reload()` won't work. |
| Navigator | ❌ Missing | `navigator.userAgent`, `navigator.geolocation`, `navigator.language` not implemented. |
| History | ❌ Missing | `window.history.back()`, `window.history.forward()`, `window.history.pushState()`, `window.history.replaceState()` not implemented as JS bindings. |

**DOM binding gaps:**
- `native_append_child` at dom_bindings.cpp:157 is a **stub** — it returns undefined without modifying the DOM
- `set_up_document_methods` only implements `getElementById` — no `createElement`, `querySelectorAll`, etc.
- No `Event` constructor or `CustomEvent` — `new Event('click')` returns undefined
- No `MutationObserver` — modern frameworks depend on it
- No `IntersectionObserver` — lazy loading doesn't work
- No `ResizeObserver` — responsive components don't work
- No `requestAnimationFrame` bound to the JS global (timers.cpp has rAF but it may not be wired)
- No `requestIdleCallback`
- No `structuredClone`
- No `queueMicrotask` on the global scope
- No `customElements.define()` — Web Components v1 not implemented
- No `ShadowRoot` / `attachShadow()`
- No `MutationRecord` / `MutationObserver`
- No `TransitionEvent`
- No `AnimationEvent`
- No `InputEvent` / `CompositionEvent`
- No `ClipboardEvent`
- No `DragEvent`
- No `PointerEvent` — only mouse events
- No `TouchEvent` / `Touch` / `TouchList`
- No `ScrollEvent` / `wheel` event handling may be incomplete
- No `FocusEvent`
- No `KeyboardEvent` key property extensions (key, code, location)
- No `MouseEvent` offsetX/offsetY, movementX/Y
- No `Element.getBoundingClientRect()`, `getComputedStyle()`, `querySelectorAll()`
- No `Document.querySelectorAll()`, `getElementsByClassName()`, `getElementsByTagNameNS()`, `createElementNS()`
- No `DocumentFragment` creation
- No `Node.textContent`, `Node.innerHTML` setters
- No `Node.appendChild()`, `removeChild()`, `insertBefore()`, `replaceChild()`
- No `Node.cloneNode()`, `normalize()`, `isSameNode()`, `isEqualNode()`
- No `ParentElement.append()`, `prepend()`, `replaceWith()`, `remove()`
- No `Element.classList`, `Element.closest()`, `Element.matches()`
- No `Element.dataset`, `Element.scrollIntoView()`
- No `Element.offsetParent`, `Element.offsetTop/Left/Width/Height`
- No `Element.scrollTop/Left/Width/Height`, `Element.clientTop/Left/Width/Height`
- No `Document.activeElement`, `Document.visibilityState`, `Document.hidden`
- No `window.getSelection()`, `Selection` API
- No `document.execCommand()` (deprecated but used by some editors)

### 6. Rendering (render/) — 50 files, ~17.7K LOC

| Component | Status | Gap |
|-----------|--------|-----|
| Renderer (renderer.hpp/cpp) | ✅ Working | OpenGL 3.3 batched quad rendering, orthographic projection, FBO blur. |
| Shader program (shader_program.hpp/cpp) | ✅ Working | Compile/link, uniform setting. |
| Mesh (mesh.hpp/cpp) | ✅ Working | VBO/IBO/VAO, batching. |
| Texture (texture.hpp/cpp) | ✅ Working | GL texture create/upload/bind. |
| Paint system (paint/) | ✅ Working | Display commands, painter, executor, gradients, shadows. |
| Font/TrueType (font/) | ✅ Working | cmap/head/hhea/hmtx/loca/glyf/kern parsing, 4x4 supersampled rasterizer, SDF. |
| Glyph atlas (atlas.hpp/cpp) | ✅ Working | Packing, caching, texture upload. |
| Embedded font (embedded.hpp/cpp) | ✅ Working | Open Sans Regular, 122KB, 1086 glyphs. |
| Emoji (emoji.hpp) | ⚠️ Partial | 30 monochrome 16x16 bitmaps. |
| Canvas (canvas.hpp/cpp) | ⚠️ Partial | Paths, fills, strokes, text — may be missing some canvas API. |
| SVG (svg_renderer.hpp/cpp) | ⚠️ Partial | rect, circle, path, text — may not support all SVG features. |
| MathML (mathml_stub.hpp/cpp) | ❌ Stub | Basic rendering only. |
| Form controls (form_controls.hpp/cpp) | ⚠️ Partial | Input/button/checkbox/radio/select/date-time pickers. |
| Audio element (audio_element.hpp/cpp) | ⚠️ Partial | May not be fully functional. |
| Video element (video_element.hpp/cpp) | ❌ Stub | Stub only. |
| Icons (icons.hpp) | ✅ Working | Embedded SVG icons for chrome UI. |
| Canvas bindings (canvas_bindings.cpp) | ⚠️ Partial | JS bindings exist but may be incomplete. |

**Rendering gaps:**
- MathML is a stub — not usable
- Video element is a stub — no playback
- SVG filters not implemented
- SVG clip-path / mask not implemented
- SVG text layout may be incomplete
- Canvas compositing modes may be limited
- Canvas gradients may be limited
- Canvas shadow may not work correctly
- Canvas text metrics may be incomplete
- Font fallback for missing glyphs only uses TOFU (30 emoji + empty) — no system font fallback
- No WOFF/WOFF2 font format support (only TrueType)
- No color emoji support (only monochrome)
- No subpixel positioning for text
- No text justification
- No letter-spacing/word-spacing edge cases
- No text-overflow: ellipsis
- No white-space: pre-wrap/pre-line edge cases
- No writing-mode support for vertical text layout
- No CSS multi-column gap/balance edge cases
- No CSS clip-path, shape-outside
- No CSS will-change, contain
- No CSS content-visibility
- No CSS scrollbar-gutter
- No CSS overscroll-behavior

### 7. Browser Application (browser/) — 32 files, ~5900 LOC

| Component | Status | Gap |
|-----------|--------|-----|
| Window/chrome (chrome/) | ✅ Working | Tabs, URL bar, buttons, text selection, textarea resize. |
| Navigator (navigator.cpp) | ✅ Working | Back/forward/refresh/navigate with search engine fallback. |
| Page loader (page_loader.cpp) | ⚠️ Partial | Async pipeline: fetch→decompress→parse→layout→paint. @font-face loading. Script execution (BR-C11). |
| Settings (settings.cpp) | ✅ Working | Homepage, search engine, proxy, theme, font size, zoom. |
| History (history.cpp) | ✅ Working | Back/forward navigation. |
| Bookmarks (bookmarks.cpp) | ✅ Working | File persistence. |
| Telemetry (telemetry.cpp) | ✅ Working | Local perf counters, about:performance. |
| Download manager (download_manager.cpp) | ⚠️ Partial | Downloads, progress, blocklist. |
| Find bar (find_bar.cpp) | ✅ Working | In-page find. |
| DevTools (devtools.cpp) | ❌ Stub | Console, Elements, Network stubs. |
| Session (session.cpp) | ✅ Working | Save/restore tabs and history. |
| Theme (theme.hpp) | ✅ Working | Color theme constants. |
| Perf counter (perf_counter.cpp) | ✅ Working | QPC-based timers. |
| Escape/dialogs (escape.hpp, dialogs.hpp) | ✅ Working | Dialog handling. |

**Browser application gaps:**
- **DevTools is a stub** — no working Elements inspector, Network panel, or Console
- **No bookmarks UI** in chrome (only data persistence)
- **No extensions API** — no `chrome.*` or `browser.*` APIs
- **No popup/blocker UI** — tracker blocking works silently with no indicator
- **No tab dragging/reordering**
- **No tab pinning**
- **No tab groups**
- **No search suggestions** in address bar
- **No autocomplete** in URL bar
- **No form autofill**
- **No password manager**
- **No reading mode**
- **No print support** (no print CSS media, no print dialog)
- **No PDF viewing**
- **No spell check**
- **No accessibility API** (no MSAA/UIAutomation integration)
- **No right-click context menu** — no DOM contextmenu event, no browser context menu
- **No drag and drop from browser** — no drag events on page elements
- **No file upload** — no `<input type="file">` file dialog integration
- **No download progress UI** properly shown
- **No SSL certificate viewer** — users can't inspect certificate details
- **No bookmark bar**
- **No sidebar**
- **No find bar** result navigation (only highlights)

### 8. Platform (platform/) — 8 files, ~1200 LOC

| Component | Status | Gap |
|-----------|--------|-----|
| Window (window_win32.hpp/cpp) | ✅ Working | Win32 window creation, WGL context, event loop, fullscreen. |
| OpenGL (opengl.hpp/cpp) | ✅ Working | GL 3.3+ function pointer loading via wglGetProcAddress. |
| Audio (audio.hpp/cpp) | ✅ Working | WAV loading, waveOut playback. |

**Platform gaps:**
- No high-DPI/DPi scaling support (DPI awareness set but no scaling of resources)
- No IME (Input Method Editor) support for CJK/Arabic input
- No accessibility (MSAA/UIAutomation)
- No HDR rendering support
- No multi-monitor DPI awareness
- No dark mode theme detection from Windows settings
- No taskbar progress indicator
- No thumbnail toolbar buttons
- No touchscreen gesture support
- No pen/stylus input
- No gamepad API
- No screen capture
- No clipboard reading (only copy-to-clipboard for Ctrl+C)
- No drag-to-external-app support

### 9. Image Decoding (image/) — 7 files, ~1300 LOC

| Component | Status | Gap |
|-----------|--------|-----|
| BMP decoder | ✅ Working | 1/4/8/16/24/32 bpp, RLE8. |
| PNG decoder | ✅ Working | Deflate, filters, palette, RGBA. |
| GIF decoder | ✅ Working | LZW, palette, interlace. |
| JPEG decoder | ✅ Working | Huffman, IDCT, YCbCr upsampling. |
| Format detection | ✅ Working | detect_format(). |
| Decoder factory | ✅ Working | Factory dispatch. |

**Image gaps:**
- No WebP support (Google's preferred web format)
- No AVIF support
- No SVG rendering integration (svg_renderer is separate, not used as an image decoder)
- No animated WebP support
- No progressive JPEG display
- No ICC color profile support
- No alpha premultiplication
- No color space conversion (all decoded as RGBA)

### 10. Crypto (net/crypto/) — Hand-written primitives

| Component | Status | Gap |
|-----------|--------|-----|
| ChaCha20 | ✅ Working | Stream cipher. |
| Poly1305 | ✅ Working | MAC. |
| AES | ✅ Working | AES-128/256, key expansion, GCM. |
| SHA | ✅ Working | SHA-256, SHA-384, HMAC, HKDF. |
| BigNum | ✅ Working | Mod arithmetic, exp, inv, random. |
| ECC | ✅ Working | secp256r1, secp384r1 point ops. |
| X25519 | ✅ Working | Montgomery ladder key exchange. |
| ECDSA verify | ✅ Working | Certificate signature verification. |

**Crypto gaps:**
- No RSA signature verification (only ECDSA and RSA-PSS in cert_verify)
- No Ed25519 signature verification
- No HKDF-Expand-Label for TLS 1.3以外的用途
- No constant-time comparison guarantee (timing attack risk in some paths)
- No secure memory zeroing (explicit) on some buffers

### 11. Tests (tests/) — 44 files, ~9300 LOC

| Component | Status | Gap |
|-----------|--------|-----|
| Test framework | ✅ Working | TEST() / ASSERT_* macros. |
| 40 test executables | ✅ Working | All subsystems covered. |
| External harness | ✅ Working | ~260 test HTML/CSS fixtures, jsdom/postcss reference comparison. |
| C++ unit tests | ✅ Working | builtins_test, layout_test, html_test, etc. |

**Test gaps:**
- **No integration tests for the full page load pipeline** (fetch → decode → parse → layout → paint → JS)
- **No network-level tests** (TLS handshake, HTTP/2 frame exchange, DNS resolution)
- **No regression tests for JavaScript behavior** beyond builtins
- **No cross-frame/cross-origin tests**
- **No error recovery tests** (malformed server responses, network interruptions)
- **No performance benchmarks** (telemetry exists but no regression tracking)
- **No accessibility tests**
- **No keyboard navigation tests**
- **No touch/pointer event tests**
- **No real-browser rendering comparison tests** (only structural/property checks, not visual pixel comparison)

---

## Priority Fixes — Status

### P0 — Must fix (page won't load) — ALL DONE

1. ~~**Add network debugging/logging.**~~ DONE — `BROWSER_NET_DEBUG=1` traces the whole pipeline; `--screenshot <url>` gives a headless repro.
2. ~~**Fix TLS certificate verification.**~~ Non-issue in practice — the Win32 CryptoAPI path validates google.com's chain on this machine. Keep an eye on it for other sites.
3. ~~**Fix the `native_append_child` stub.**~~ DONE — implemented with correct ownership transfer (`html::detach_from_parent`).
4. ~~**Fix the async pipeline abandonment bug.**~~ Already addressed by BR-N2/N3 detach-on-launch; no further failures observed.
5. ~~**Fix `display_background_color.html`**~~ DONE — it was the renderer texture-state bug (fills sampled a stale image texture); 90/90 now.

### P1 — Must fix (google.com won't render properly)

6. ~~**Implement a real RegExp engine**~~ DONE (2026-08-29) — `js/regex_engine.cpp` backtracking engine + `regexp.cpp` bindings; 25/26 on the rtest verification page (only the `unicode-cp` check mismatches, and that is a wrong test expectation). Remaining gaps: named groups, `$<name>`, `/u` astral codepoints.

7. **Implement `window.location`** — `location.href`, `location.reload()`, `location.assign()`, `location.replace()` must be wired to the browser's navigation system.

8. **Implement `document.createElement()`, `document.createTextNode()`, `document.createDocumentFragment()`** — essential for DOM construction via JS.

9. **Implement `Node.appendChild()`, `Node.removeChild()`, `Node.insertBefore()`** — appendChild is real now (ownership-transferring); removeChild/insertBefore remain.

10. **Implement `Element.querySelectorAll()`** — current `querySelector` only finds by tag name (not a CSS selector).

11. **Implement `addEventListener` with proper Event object creation** — current implementation fires handlers but doesn't create proper `Event` objects with `preventDefault()`, `stopPropagation()`, etc.

12. **Implement `Promise` properly** — the current `make_promise_object` creates a plain object with `[[PromiseState]]`/`[[PromiseResult]]` but doesn't implement the actual Promise spec (then/catch/finally chain, microtask queue, rejection tracking).

13. **Wire the module loader** — `js/module_loader.hpp/cpp` exists but is not connected to `script_runner`.

### P2 — Needed for modern websites

14. **Implement Map and Set** — used extensively by modern frameworks.

15. **Implement Proxy and Reflect** — used by many libraries and frameworks.

16. **Implement BigInt** — needed by some crypto and computation libraries.

17. **Implement Iterator/Generator** — `for...of`, `yield`, spread syntax depend on this.

18. **Implement MutationObserver** — used by frameworks for DOM change detection.

19. **Implement IntersectionObserver** — used for lazy loading images and components.

20. **Implement ResizeObserver** — used by responsive components.

21. **Implement requestAnimationFrame properly** — wired to the browser's render loop.

22. **Implement fetch() properly** — the current implementation returns a plain object instead of a real `Response` object with proper methods and a real Promise chain.

23. **Implement XMLHttpRequest properly** — current bindings may be incomplete.

24. **Implement WebSocket properly** — current implementation may not be integrated into the page pipeline.

25. **Implement localStorage/sessionStorage** — wired to the `net::Storage` backend.

26. **Implement Console API fully** — current console works but `console.table`, `console.timeLog`, `console.groupEnd` may have issues.

27. **Implement URL and URLSearchParams** — needed for many websites.

28. **Implement Blob and File** — needed for file upload and data URLs.

29. **Implement FormData** — needed for form submission via JS.

30. **Implement customElements.define()** — Web Components v1.

### P3 — Nice to have for completeness

31. **Implement WebAssembly** — `wasm` module loading and execution.

32. **Implement Web Workers** — background thread execution.

33. **Implement SharedArrayBuffer and Atomics** — needed for parallel computation.

34. **Implement Notification API** — desktop notifications.

35. **Implement Geolocation API** — `navigator.geolocation`.

36. **Implement Clipboard API** — `navigator.clipboard`.

37. **Implement WebRTC** — real-time communication.

38. **Implement IndexedDB** — client-side database.

39. **Implement Web Crypto API** — `crypto.subtle`.

40. **Implement Push API** — web push notifications.

41. **Implement Background Sync** — offline sync.

42. **Implement Service Worker** — offline caching, network proxying.

43. **Implement Payment Request API** — e-commerce checkout.

44. **Implement WebAuthn** — passwordless authentication.

45. **Implement File System Access API** — direct file system access.

46. **Implement WebGPU** — modern GPU compute/rendering.

47. **Implement Speech Recognition and Synthesis** — voice features.

48. **Implement Media Capabilities** — media format detection.

49. **Implement Digital Credentials API** — digital identity.

50. **Implement Private State Tokens** — privacy-preserving ads.

---

## Test Pass Status

The harness shows **90/90 passing** (2026-08-29). Sixteen layout/display-list fixtures were regenerated this session because the engine's output became *more* correct (inline children offset by their own border/padding, shrink-to-fit for floats and inline-blocks, table columns honoring explicit widths, no duplicated `text_lines`). Fixture regeneration must happen **after** clang-format (see warning at top).

The RegExp round (separate commit, 2026-08-29) left the harness at **90/90** (no regex in harness fixtures' expected output) and all **42** `*_test.exe` green. The standalone 26-check rtest page is **25/26** (only `unicode-cp` mismatches, a wrong test expectation).

---

## Build Status

- `ninja -C build` — succeeds with zero warnings; all 42 C++ test executables pass
  (note: `iocp_test.exe` has a rare pre-existing teardown segfault *after* all its
  tests pass — flaky, unrelated to rendering/layout code)
- `git clang-format --diff HEAD` — clean on all changed files
- clang-tidy 21 cannot fully parse the g++ build (`-fcoroutines` is a GCC-only
  flag); the two warnings it found in changed layout code (dead `line_w` store,
  unread `float_right_x`) were fixed — the latter also fixed overlapping right
  floats. Pre-existing analyzer notes in untouched headers (e.g. `CSSGradient::type`
  uninitialized) are not addressed here.

---

## Summary

The browser **loads and renders google.com** end-to-end: network stack (DNS, TLS 1.3, HTTP/2, gzip), HTML5 parsing, cascade with google's real stylesheets, layout (table-based search form, floats, inline runs), and paint (logo image, form controls, centered footer). The one failing visual area on google.com is the `#gb` header (flex-in-float collapse + unresolved `var()` chains + unpainted inline SVG).

The gap from "loads google.com" to "fully complete browser" is now:
1. **Flexbox-in-float and CSS custom property resolution** — google's header/toolbar CSS depends on both
2. **SVG painting** — icons/logos across the web
3. **JavaScript API completeness** — real RegExp is now landed (2026-08-29); still missing `createElement`, `querySelectorAll`, `location`, `Promise`, `Map`/`Set`, ES modules — required for sites that need JS
4. **Runtime robustness** on other real sites (brotli is the notable content-encoding gap — many CDNs serve `br` to modern UAs)