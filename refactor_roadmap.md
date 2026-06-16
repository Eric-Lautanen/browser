# Refactor Roadmap: Modular Codebase Restructure

## Philosophy

The browser's codebase grew organically through rapid feature development. Several files have exceeded manageable size, making them hard to navigate, debug, and extend. This roadmap splits large files into focused, single-responsibility modules — no behavior changes, no new features, just cleaner structure.

### Rules

| Rule | Detail |
|------|--------|
| **No behavior changes** | Every refactor step must produce identical compiled output. Tests must pass before and after. |
| **One split per step** | Each phase splits exactly one large file. Commit, test, move to next. Never split multiple files in one pass. |
| **Header hygiene** | Move type declarations alongside their implementation. Keep the public API header lean. Forward-declare where possible. |
| **Circular deps forbidden** | The split must not introduce circular `#include` dependencies between sibling files in the same folder. |
| **~600 line limit** | No logic file should exceed ~600 lines. Data tables (entities, font bytes) are exempt. |
| **Phase completion checklist applies** | Same as main roadmap: build clean, clang-format, clang-tidy, all tests pass, committed, pushed. |

---

## Phase R1: `css/layout/` — Split the 2,448-line Layout Monolith

**Goal**: Break `css/layout.cpp` into focused files per layout mode. This is the single highest-impact refactor.

### R1.1 — Extract types into `css/layout/types.hpp`

Move type definitions out of `layout.hpp` into a dedicated header so every layout file can include just the types without pulling in the whole `LayoutEngine` class.

| From `css/layout.hpp` | To `css/layout/types.hpp` |
|------------------------|--------------------------|
| `EdgeSizes` | `struct EdgeSizes { f32 top, right, bottom, left; };` |
| `Rect` + `Rect::intersect()` | `struct Rect { f32 x,y,w,h; std::optional<Rect> intersect(const Rect&) const; };` |
| `Mat3x3` + all helpers | `struct Mat3x3 { ... static identity(), from_values(), translate(), rotate(), ... };` |
| All `enum class` values | `FlexDirection`, `FlexWrap`, `JustifyContent`, `AlignItems`, `AlignSelf`, `AlignContent`, `DisplayType`, `PositionType`, `OverflowType` |
| `GridTrackDef` forward decl | `struct GridTrackDef;` |

### R1.2 — Extract resolve functions into `css/layout/resolve.cpp`

Move all CSS value resolution out of the massive layout file. These are pure computation with no layout state.

| Function | File |
|----------|------|
| `LayoutEngine::resolve_clamp_func` | `resolve.cpp` |
| `LayoutEngine::resolve_font_size` | `resolve.cpp` |
| `LayoutEngine::resolve_calc_string` | `resolve.cpp` |
| `LayoutEngine::resolve_length` | `resolve.cpp` |
| `LayoutEngine::resolve_func_length` | `resolve.cpp` |
| `LayoutEngine::resolve_side_value` | `resolve.cpp` |
| `LayoutEngine::resolve_property` | `resolve.cpp` |

### R1.3 — Extract type checks into `css/layout/type_check.cpp`

Move element classification functions. These are pure predicates, no layout logic.

| Function | File |
|----------|------|
| `LayoutEngine::is_block_element` | `type_check.cpp` |
| `LayoutEngine::is_inline_element` | `type_check.cpp` |
| `LayoutEngine::is_positioned` | `type_check.cpp` |
| `LayoutEngine::is_fixed_or_sticky` | `type_check.cpp` |
| `LayoutEngine::is_flex_element` | `type_check.cpp` |
| `LayoutEngine::is_grid_element` | `type_check.cpp` |
| `LayoutEngine::is_table_element` | `type_check.cpp` |
| `LayoutEngine::creates_stacking_context` | `type_check.cpp` |
| `LayoutEngine::get_z_index` | `type_check.cpp` |

### R1.4 — Extract flexbox into `css/layout/flex.cpp`

Flexbox is ~300 lines of self-contained logic. Move `layout_flex` and all its helpers.

| Function | File |
|----------|------|
| `is_row_direction`, `is_reverse_direction` | `flex.cpp` |
| `LayoutEngine::distribute_lines` | `flex.cpp` |
| `LayoutEngine::distribute_free_space` | `flex.cpp` |
| `LayoutEngine::compute_cross_sizes` | `flex.cpp` |
| `LayoutEngine::align_lines` | `flex.cpp` |
| `LayoutEngine::layout_flex` | `flex.cpp` |

Keep `layout.hpp` declaring `layout_flex` as a public method. Move the implementation.

### R1.5 — Extract grid into `css/layout/grid.cpp`

Grid is already partially separated (`css/grid.cpp`), but `layout_grid` lives in `layout.cpp`. Move it.

| Function | File |
|----------|------|
| `GridState` struct | `grid.cpp` |
| `LayoutEngine::resolve_grid_tracks` | `grid.cpp` |
| `LayoutEngine::layout_grid` | `grid.cpp` |

### R1.6 — Extract block/inline/table into `css/layout/block.cpp`, `inline.cpp`, `table.cpp`

| Function | File |
|----------|------|
| `LayoutEngine::layout_block` | `block.cpp` |
| `LayoutEngine::layout_inline` | `inline.cpp` |
| `is_table_cell_tag`, `is_table_row_tag` | `table.cpp` |
| `LayoutEngine::layout_table` | `table.cpp` |

### R1.7 — Extract positioning into `css/layout/positioning.cpp`

| Function | File |
|----------|------|
| `LayoutEngine::layout_absolute_pass` | `positioning.cpp` |
| `apply_transform_to_node` | `positioning.cpp` |

> **Note**: `float`/`clear` and `overflow: scroll` logic is currently inlined within `layout_block` and `layout_children` — not separable as standalone functions in this pass. Stacking context sorting is handled by `creates_stacking_context` and `get_z_index` (moved in R1.3) plus paint-order logic that lives in `render/painter.cpp`. These can be extracted in a future refactor once the layout engine stabilizes.

### R1.8 — Clean up `layout/engine.cpp`

After all extractions, `engine.cpp` keeps only:

- `LayoutEngine` constructor/destructor
- `LayoutEngine::build_layout_tree` (tree construction is complex enough to stay)
- `LayoutEngine::layout` (main entry point that dispatches to sub-engines)
- `LayoutEngine::set_text_measure`
- `LayoutEngine::layout_children`

Validate that `engine.cpp` is now under ~600 lines.

### Phase R1 Checklist
- [ ] `css/layout/types.hpp` contains all extracted types. Old `layout.hpp` includes it.
- [ ] `css/layout/resolve.cpp` — all 7 resolve functions moved; `engine.cpp` calls them via `LayoutEngine::` prefix
- [ ] `css/layout/type_check.cpp` — all 9 predicate functions moved, dispatch intact
- [ ] `css/layout/flex.cpp` — flexbox layout + 5 helpers moved, no circular deps
- [ ] `css/layout/grid.cpp` — grid layout moved, GridState internal
- [ ] `css/layout/block.cpp` — block layout moved
- [ ] `css/layout/inline.cpp` — inline layout moved
- [ ] `css/layout/table.cpp` — table layout moved
- [ ] `css/layout/positioning.cpp` — absolute/fixed/sticky positioning moved
- [ ] `css/layout/engine.cpp` — under ~600 lines; `build_layout_tree` and `layout` dispatcher only
- [ ] `CMakeLists.txt` — `css/layout/` folder added as source group; all new .cpp files listed; `css/layout.hpp` still public
- [ ] No circular includes between any `css/layout/*.cpp` files
- [ ] `cmake --build build` succeeds with no warnings
- [ ] All pre-existing tests pass identically before and after
- [ ] clang-format and clang-tidy pass
- [ ] Committed and pushed

---

## Phase R2: `css/parser/` — Split the 953-line CSS Parser

**Goal**: Separate selector parsing, declaration parsing, at-rule parsing, and property handling into focused files.

| File | Contents |
|------|----------|
| `css/parser/parser.hpp` | Public API: `css::parse(string) -> StyleSheet`. |
| `css/parser/parser.cpp` | Top-level dispatch, rule list parsing. ~150 lines. |
| `css/parser/selector.cpp` | Selector parsing (compound, complex, pseudo-class, pseudo-element, attribute). ~200 lines. |
| `css/parser/declaration.cpp` | Declaration + value parsing (primitives, functions, lists). ~200 lines. |
| `css/parser/at_rule.cpp` | `@media`, `@keyframes`, `@font-face`, `@import`, `@supports`, `@charset`. ~200 lines. |
| `css/parser/property.cpp` | Property name recognition, shorthand expansion maps. ~150 lines. |

### Phase R2 Checklist
- [ ] `css/parser/parser.cpp` under ~200 lines
- [ ] `selector.cpp` parses all selector types correctly
- [ ] `declaration.cpp` handles all value types (length, color, string, func, list, calc)
- [ ] `at_rule.cpp` handles all `@` rules
- [ ] `property.cpp` handles all shorthand expansions
- [ ] All tests pass, no regressions
- [ ] Committed and pushed

---

## Phase R3: `css/cascade/` — Split the 783-line Cascade

**Goal**: Separate cascade computation, inheritance, specificity, `!important`, and `@media` evaluation.

| File | Contents |
|------|----------|
| `css/cascade/engine.hpp` | Public API: `Cascade`, `ComputedStyles`. |
| `css/cascade/engine.cpp` | `Cascade::compute` entry point, style resolution loop. ~150 lines. |
| `css/cascade/specificity.cpp` | Specificity sorting of matched rules. ~100 lines. |
| `css/cascade/important.cpp` | `!important` flag handling, origin priority reversal. ~100 lines. |
| `css/cascade/inheritance.cpp` | Property inheritance (initial values, inherit keyword, inherited properties). ~150 lines. |
| `css/cascade/media.cpp` | `@media` condition evaluation, re-evaluation on resize/theme change. ~150 lines. |

### Phase R3 Checklist
- [ ] All cascade logic moved, dispatch preserved
- [ ] Specificity + !important correct: UA < author < inline, important reverses
- [ ] Inheritance chain correct for all inherited properties
- [ ] `@media` conditions re-evaluated on resize
- [ ] All tests pass
- [ ] Committed and pushed

---

## Phase R4: `net/http2/` — Split the 1,057-line HTTP/2 Client

**Goal**: Separate frame encoding/decoding, HPACK, connection management, and stream state.

| File | Contents |
|------|----------|
| `net/http2/connection.hpp/cpp` | `HTTP2Client` class, connection lifecycle, stream table. ~250 lines. |
| `net/http2/frames.cpp` | Frame encode/decode for all frame types (DATA, HEADERS, SETTINGS, PING, GOAWAY, WINDOW_UPDATE, RST_STREAM, PRIORITY, PUSH_PROMISE, CONTINUATION). ~350 lines. |
| `net/http2/hpack.cpp` | HPACK encoder/decoder (already partially separate, but embedded in http2.cpp). ~300 lines. |

### Phase R4 Checklist
- [ ] Frame encode/decode for all 10 frame types work identically
- [ ] HPACK round-trips match previous output
- [ ] Connection multiplexing works (stream IDs, concurrent streams)
- [ ] All net tests pass
- [ ] Committed and pushed

---

## Phase R5: `net/tls/` — Split the 987-line TLS Stack

**Goal**: Separate handshake state machine, record layer, and cipher dispatch.

| File | Contents |
|------|----------|
| `net/tls/handshake.hpp/cpp` | TLS 1.3 handshake state machine (ClientHello → ServerHello → Cert → CertVerify → Finished). ~350 lines. |
| `net/tls/record.cpp` | Record layer: `send_raw_record`, `read_raw_record`, fragmentation, content type dispatch. ~200 lines. |
| `net/tls/cipher.cpp` | AEAD encryption/decryption dispatch (AES-GCM, ChaCha20-Poly1305), key schedule. ~250 lines. |
| `net/tls/connection.hpp/cpp` | `TLSConnection` class, public API, state management. ~150 lines. |

### Phase R5 Checklist
- [ ] Handshake, record, cipher all split with no circular deps
- [ ] TLS 1.3 handshake completes to real servers
- [ ] All tls_test tests pass
- [ ] Committed and pushed

---

## Phase R6: `browser/chrome/` — Split the 910-line BrowserWindow

**Goal**: Separate window lifecycle, titlebar rendering, toolbar rendering, page view, event handling, and navigation.

| File | Contents |
|------|----------|
| `browser/chrome/window.hpp/cpp` | `BrowserWindow` class shell, `initialize()`, `run()`, destructor. ~150 lines. |
| `browser/chrome/titlebar.cpp` | `render_tabs()`, `render_caption_buttons()`, tab close buttons, tab switching. ~150 lines. |
| `browser/chrome/toolbar.cpp` | `render_nav_buttons()`, `render_address_bar()`, `render_bookmark()`, `render_menu_button()`. ~200 lines. |
| `browser/chrome/page_view.cpp` | `render_page()`, `render_scrollbar()`, `render_settings()`. ~150 lines. |
| `browser/chrome/event_handler.cpp` | `handle_event()`, `handle_mouse_click()`, `handle_key_down()`, `handle_scroll()`, `handle_mouse_move()`. ~250 lines. |
| `browser/chrome/navigator.cpp` | `navigate()`, `navigate_back()`, `navigate_forward()`, `refresh()`, `new_tab()`, `close_tab()`. ~100 lines. |

### Phase R6 Checklist
- [ ] BrowserWindow still initializes and runs correctly
- [ ] All chrome rendering unchanged
- [ ] All event handling unchanged
- [ ] Tab operations work identically
- [ ] All chrome_test tests pass
- [ ] Committed and pushed

---

## Phase R7: `render/font/` — Split the 655-line + 631-line Font Files

**Goal**: Separate TrueType parsing, glyph rasterization, atlas management, and embedded font data.

| File | Contents |
|------|----------|
| `render/font/font.hpp/cpp` | `Font`, `FontManager` public API, `load_from_file`, `load_from_memory`, `get_glyph_index`, metrics. ~200 lines. |
| `render/font/truetype.cpp` | TrueType table parsing (`cmap`, `head`, `hhea`, `hmtx`, `kern`, `glyf`, `loca`). ~300 lines. |
| `render/font/rasterizer.cpp` | Glyph rasterization, scanline fill, bezier flattening, hinting. ~300 lines. |
| `render/font/atlas.cpp` | Glyph atlas management, texture caching, `render_text`, `measure_text`. ~200 lines. |
| `render/font/embedded.cpp` | Embedded default font data (compressed TTF). ~631 lines → stays large, it's data. |

### Phase R7 Checklist
- [ ] All TrueType parsing functions moved to `truetype.cpp`
- [ ] Rasterizer functions moved to `rasterizer.cpp`
- [ ] Atlas + text rendering moved to `atlas.cpp`
- [ ] Embedded font data stays as-is
- [ ] All font_test tests pass
- [ ] Committed and pushed

---

## Phase R8: `html/parser/` — Split the 1,933-line HTML Parser

**Goal**: Separate insertion mode handlers, foster parenting, adoption agency algorithm into focused files.

| File | Contents |
|------|----------|
| `html/parser/parser.hpp` | Public API: `html::parse(string) -> unique_ptr<Document>`. |
| `html/parser/parser.cpp` | Token consumer loop, insertion mode dispatch. ~250 lines. |
| `html/parser/initial.cpp` | Initial insertion mode (DOCTYPE handling). ~100 lines. |
| `html/parser/before_html.cpp` | Before HTML / After HTML insertion modes. ~100 lines. |
| `html/parser/head.cpp` | Head insertion mode + after head. ~150 lines. |
| `html/parser/body.cpp` | Body insertion mode + text/table/select sub-modes. ~400 lines. |
| `html/parser/frameset.cpp` | Frameset insertion modes. ~100 lines. |
| `html/parser/after_body.cpp` | After body / after after body / after after frameset. ~100 lines. |
| `html/parser/foster_parenting.cpp` | Foster parenting algorithm for table-related content. ~100 lines. |
| `html/parser/adoption_agency.cpp` | Adoption agency algorithm for misnested formatting elements. ~150 lines. |
| `html/parser/template.cpp` | Template content model insertion. ~100 lines. |

### Phase R8 Checklist
- [ ] Each insertion mode group has its own file
- [ ] Foster parenting and adoption agency are self-contained
- [ ] Template handling separated
- [ ] All html_test tests pass
- [ ] No circular includes
- [ ] Committed and pushed

---

## Phase R9: `net/socket/` — Split Socket Implementations

**Goal**: Separate TCP socket, UDP socket, address types.

| File | Contents |
|------|----------|
| `net/socket/types.hpp/cpp` | `IPv4Address`, `IPv6Address` + `to_string`, `from_string`, operators. |
| `net/socket/tcp.cpp` | `Win32TCPSocket` — `connect`, `connect_ip`, `send`, `send_all`, `receive`, `receive_all`, `set_read_timeout`, async overlapped methods. |
| `net/socket/udp.cpp` | `Win32UDPSocket` — `send`, `receive`, `set_read_timeout`, async overlapped methods. |

### Phase R9 Checklist
- [ ] TCP + UDP split, no shared mutable state
- [ ] All net_test tests pass
- [ ] Committed and pushed

---

## Phase R10: `render/paint/` — Split Painter and Executor

**Goal**: Separate display list generation, display list execution, gradient rendering, shadow rendering.

| File | Contents |
|------|----------|
| `render/paint/commands.hpp` | `DisplayCommand` enum + union types. ~100 lines. |
| `render/paint/painter.cpp` | Display list generation from layout tree. ~200 lines. |
| `render/paint/executor.cpp` | Display list execution (OpenGL draw calls). ~200 lines. |
| `render/paint/gradient.cpp` | Gradient texture generation (linear, radial, conic). ~150 lines. |
| `render/paint/shadow.cpp` | Box-shadow and text-shadow blur rendering. ~150 lines. |

### Phase R10 Checklist
- [ ] Painter generates identical display lists
- [ ] Executor renders identically
- [ ] Gradients and shadows separated
- [ ] All paint_test tests pass
- [ ] Committed and pushed

---

## Phase R11: `js/parser/` — Split the 1,041-line JS Parser

**Goal**: Separate expression parsing, statement parsing, declaration parsing, and pattern parsing into focused files.

| File | Contents |
|------|----------|
| `js/parser/parser.hpp` | Public API: `js::parse(Tokenizer&) -> Program`. |
| `js/parser/parser.cpp` | Top-level `parse_program()`, token stream management, error recovery. ~150 lines. |
| `js/parser/expression.cpp` | Expression parsing (literals, binary, unary, call, member, assignment, arrow, object/array literals, template literals, optional chaining). ~300 lines. |
| `js/parser/statement.cpp` | Statement parsing (block, if, while, for, var/let/const, function decl, return, break, throw, try/catch, empty). ~250 lines. |
| `js/parser/declaration.cpp` | Declaration parsing (`import`, `export`, `class`). ~150 lines. |
| `js/parser/pattern.cpp` | Destructuring pattern parsing (identifier, object, array). ~100 lines. |

### Phase R11 Checklist
- [ ] Expression parsing separated, no regressions on any expression types
- [ ] Statement parsing separated, all statement types work
- [ ] Declaration parsing separated, import/export/class work
- [ ] Pattern parsing separated, destructuring works
- [ ] All js_test, parser_test, compiler_test tests pass
- [ ] Committed and pushed

---

## Phase R12: `js/vm/` — Split the 861-line VM + 683-line Compiler

**Goal**: Separate bytecode execution, builtins dispatch, GC integration, and compiler into focused files.

| File | Contents |
|------|----------|
| `js/vm/vm.hpp/cpp` | `VM` class, `execute()` main loop, call frame management, try/catch. ~250 lines. |
| `js/vm/ops.cpp` | Per-opcode execution helpers (arithmetic, comparison, property access, type coercion). ~250 lines. |
| `js/vm/builtins.cpp` | Builtin function dispatch table, native function registration. ~200 lines. |
| `js/vm/gc.cpp` | GC integration (root scanning, allocation, write barriers — code already in `js/gc.cpp`, but VM integration separated). ~100 lines. |
| `js/vm/compiler/compiler.hpp/cpp` | `Compiler` class, `compile()` entry point, function compilation. ~200 lines. |
| `js/vm/compiler/expr.cpp` | Expression bytecode emission. ~200 lines. |
| `js/vm/compiler/stmt.cpp` | Statement bytecode emission. ~200 lines. |

### Phase R12 Checklist
- [ ] VM, ops, builtins all split with no circular deps
- [ ] Compiler splits expression and statement emission
- [ ] All VM call frame and try/catch behavior preserved
- [ ] All builtins dispatch identically
- [ ] All compiler_test, vm_test, js_test, gc_test tests pass
- [ ] Committed and pushed

---

## Phase R13: `html/tokenizer/` — Split the 639-line HTML Tokenizer

**Goal**: Separate tokenizer states into focused files. The tokenizer is a single large state machine (89 states) that can be grouped by content model.

| File | Contents |
|------|----------|
| `html/tokenizer/tokenizer.hpp/cpp` | Public API, main `next_token()` loop, state dispatch. ~150 lines. |
| `html/tokenizer/states_data.cpp` | Data state + character reference resolution. ~100 lines. |
| `html/tokenizer/states_tag.cpp` | Tag open, tag name, end tag, self-closing, attribute states. ~150 lines. |
| `html/tokenizer/states_rcdata.cpp` | RCDATA, RAWTEXT, script data states. ~100 lines. |
| `html/tokenizer/states_foreign.cpp` | Foreign content (SVG, MathML) states. ~100 lines. |

### Phase R13 Checklist
- [ ] Tokenizer state groups separated, no regressions
- [ ] Character reference resolution still works correctly
- [ ] All html_test tests pass (including tokenizer-specific tests)
- [ ] No circular includes with html/parser/
- [ ] Committed and pushed

---

## Implementation Order

```
R1:  css/layout/           ← HIGHEST IMPACT (2,448 lines → 11 files)
R2:  css/parser/           ← 953 lines → 5 files
R3:  css/cascade/          ← 783 lines → 5 files
R4:  net/http2/            ← 1,057 lines → 3 files
R5:  net/tls/              ← 987 lines → 4 files
R6:  browser/chrome/       ← 910 lines → 6 files
R7:  render/font/          ← 655+631 lines → 5 files
R8:  html/parser/          ← 1,933 lines → 11 files
R9:  net/socket/           ← 463 lines → 3 files
R10: render/paint/         ← 397+379 lines → 5 files
R11: js/parser/            ← 1,041 lines → 5 files
R12: js/vm/                ← 861+683 lines → 7 files
R13: html/tokenizer/       ← 639 lines → 5 files
```

**Do R1 first.** It's the biggest file, the most self-contained, and the highest risk of bit-rot the longer you wait.

**Do R2 and R3 next** since they touch the same `css/` folder and you can validate cascade + parser integration together.

**Do R11, R12, R13 last** since the JS engine and HTML tokenizer are the most complex state machines and any mistake can cause subtle bugs. The project is far enough along that these files are stable — no new JS syntax or HTML features are being added.

**R8 (HTML parser) can be done anytime after R2/R3** since the parser and tokenizer are independent from the CSS refactors.

### Files not in scope (confirmed)

| File | Lines | Reason |
|------|-------|--------|
| `html/entities.cpp` | 2,237 | Data table (entity name → code point mappings). Exempt per rules. |
| `render/embedded_font.cpp` | 631 | Data (compressed TrueType font bytes). Exempt per rules. |
| `render/icons.hpp` | 309 | Data (icon SVG paths). Header-only, exempt. |
| `tests/*.cpp` | 582 max | Test files. Must remain unchanged to validate refactors have no regressions. |
| `js/lexer.cpp` | 579 | Single state machine under the ~600 line guideline. Splitting would create artificial boundaries. Monitor for growth. |
| `net/crypto/*.cpp` | 350 max | Already in their own `crypto/` folder, each file is a single algorithm. Modular enough. |

---

## Phase Completion Checklist (Every Phase)

- [ ] Split produces identical compiled output (tests pass before and after)
- [ ] `cmake --build build` succeeds with no warnings (`-Werror`)
- [ ] All pre-existing test executables pass (zero regressions)
- [ ] clang-format -n --Werror passes on all changed files
- [ ] clang-tidy -p build --extra-arg=-Wno-unused-command-line-argument produces no new warnings
- [ ] No new circular `#include` dependencies introduced
- [ ] No behavior changes — this is pure refactoring
- [ ] Committed and pushed with message format: `refactor: split css/layout/ into N files`
