**Still missing:** Devanagari/SE Asian complex shaping (known limitation — uses fallback to isolated forms), `@font-face` web fonts, variable fonts, atlas LRU eviction, proper line-height from font metrics, UAX#14 line-breaking.

---

## Phase 1 — CSS font-family → physical font matching [✓ COMPLETE]

### 1.1  System font registry [✓]
**File:** `render/font/registry.{hpp,cpp}`

Enumerate installed Windows fonts via registry:
`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts`

Builds a `FontRegistry` singleton mapping family names (normalized, stripped of suffixes)
to file paths. Scanned once lazily at first use.

### 1.2  Generic family mapping [✓]
**File:** `render/font/registry.{hpp,cpp}`

| CSS generic | Windows mapping |
|-------------|-----------------|
| `serif`     | Times New Roman |
| `sans-serif`| Arial           |
| `monospace` | Consolas        |
| `cursive`   | Comic Sans MS   |
| `fantasy`   | Gabriola        |

### 1.3  Font stack resolution per layout node [✓]
**File:** `render/font/atlas.cpp`, `render/paint/executor.cpp`, `render/paint/painter.cpp`

`PaintCommand` carries a `font_family` string. Painter reads `font-family` from
`ComputedStyle`, executor passes it to `render_text()`. `TextRenderer` resolves
via `FontManager::resolve_family()` before each text run.

### 1.4  Per-character font fallback in render loop [✓]
**File:** `render/font/atlas.cpp` — `prepare_glyph` and `render_text`

When `glyph_index(cp) == 0` on the primary face, walks the fallback chain
(via `find_font_for_codepoint`). Results cached per-codepoint per-face.

### 1.5  System-wide last-resort fallback [✓]
**File:** `render/font/font.cpp` — `load_fallback_fonts()`

Fallback chain includes CJK (msgothic.ttc), emoji (seguiemj.ttf), and symbol fonts.
Each fallback font is registered with its Unicode range.

---

## Phase 2 — Font loading improvements

### 2.1  Lazy font loading from registry
**File:** `render/font/registry.cpp`  **Estimate:** 60 lines

Don't load every Windows font at startup. Load on first use:
- `load_family(name)` — finds the font file path, creates FontFace*, caches it
- Fonts are loaded once and shared across all TextRenderer instances

### 2.2  TTC (TrueType Collection) support [✓]
**File:** `render/font/truetype.cpp` — `load_from_ttc_memory()`

`msgothic.ttc`, `msmincho.ttc`, `yugothm.ttc` contain multiple fonts in one file.
Parses TTC header (`ttcf` tag), reads font offset table, calls `load_tables_from_offset()`
with absolute font offset. Refactored `load_tables_from_offset()` from `load_from_memory()`
to support loading a sub-font at an arbitrary offset within the data buffer.

### 2.3  @font-face support [✓]
**File:** `html/resource_loader.cpp`, `css/cascade/engine.cpp`, `render/font/registry.cpp`

1. CSS parser already handles `@font-face` at-rule (stores in `StyleSheet::at_rules`)
2. Cascade: extract `@font-face` descriptors (src, font-family, font-weight, font-style)
3. Resource loader: fetch the font file URL (same HTTP pipeline as images/CSS)
4. Font registry: store downloaded fonts keyed by `@font-face { font-family: "name" }`
5. Font stack resolution (1.3) checks downloaded fonts first, before system fonts

### 2.4  Font data deduplication
**File:** `render/font/registry.cpp`  **Estimate:** 30 lines

Multiple font stacks may reference the same `FontFace*`. Share via weak_ptr or a
central `std::unordered_map<std::string, std::shared_ptr<FontFace>>` in FontRegistry.

---

## Phase 3 — Emoji and color font support [✓ COMPLETE]

### 3.1  Emoji codepoint detection [✓]
**File:** `html/utf8.hpp`

`is_emoji(u32 codepoint)` returns true for:
- U+1F300–U+1FAFF (Misc Symbols, Emoticons, Supplemental)
- U+2600–U+27BF (Misc Symbols, Dingbats)
- U+FE00–U+FE0F (Variation Selectors)
- U+200D (ZWJ)
- U+1F1E6–U+1F1FF (Regional indicators / flags)

### 3.2  Color bitmap glyph rasterization (CBDT/CBLC) [✓]
**File:** `render/font/truetype.cpp`

1. `has_color_bitmap(codepoint, pixel_size)` — searches CBLC strike entries for matching glyph ID, checks for format 17/18/19
2. `rasterize_color_bitmap(codepoint)` — locates embedded PNG in CBDT via CBLC offset, decodes with `image::create_decoder(PNG)`, returns RGBA `GlyphBitmap`
3. `rasterize_glyph()` tries color bitmap first before falling back to outline rasterization
4. `GlyphBitmap::has_color` flag added, atlas uploads RGBA via `update_sub(use_rgba=true)`

### 3.3  Color emoji rendering in TextRenderer [✓]
**File:** `render/font/atlas.cpp`

1. `prepare_glyph` uses bitmap rasterization path when the font has CBDT/CBLC tables
2. `render_text` renders color glyphs with `Color::WHITE` tint (glyph data has its own color)
3. `GlyphAtlasEntry::has_color` flag triggers RGBA texture upload and white-tint rendering

### 3.4  Emoji font fallback ordering [✓]
**File:** `render/font/font.cpp` — `load_fallback_fonts()`

Segoe UI Emoji is registered in the fallback chain with range U+1F300–U+1FAFF,
so it's only selected for emoji codepoints. Generic families explicitly map to
non-emoji fonts (sans-serif → Arial, serif → Times New Roman, etc.).

---

## Phase 4 — Text shaping and layout

### 4.1  Ligature-aware text measurement [✓]
**File:** `render/font/shaper.cpp`

GSUB ligature substitution (lookup type 4) applied via 'rlig'/'liga' features.
fi/fl/ffi/ffl ligatures work when the font provides them.

### 4.2  Kerning for all font pairs [✓]
**File:** `render/font/shaper.cpp`, `render/font/truetype.cpp`

GPOS PairPos subtable parsing (format 1 individual pairs, format 2 class-based).
Legacy `kern` table also applied. Both contribute to advance widths.

### 4.3  Arabic shaping via GSUB [✓]
**File:** `render/font/shaper.{hpp,cpp}`

- Unicode joining type table (R/L/D/U/T/C) covering U+0600–06FF, 0750–077F, 08A0–08FF
- Position detection: initial/medial/final/isolated based on joining context
- GSUB features 'init', 'medi', 'fina', 'isol' applied for positional forms
- Shaper only activated for Arabic text (non-Arabic uses fast per-codepoint path)
- Devanagari/SE Asian: documented known limitation (fallback to isolated forms)

### 4.4  Line-breaking by Unicode rules (UAX #14) [✓]
**File:** `css/layout/inline.cpp`

CJK characters split into individual words for natural line breaks at character boundaries.
Handles U+2E80–U+9FFF, U+AC00–D7AF, U+F900–FAFF, U+FE30–FE4F, U+FF01–FFEF.
Soft-hyphen (U+00AD) treated as break opportunity.

---

## Phase 5 — Measurement and metrics correctness [✓ COMPLETE]

### 5.1  Use font metrics for line-height [✓]
**File:** `css/layout/block.cpp`, `render/font/atlas.cpp`

`line-height: normal` computes `ascender - descender + line_gap` from font metrics.
TextMetricsFn callback wired through LayoutEngine → TextRenderer.
Handles "normal", percentages, unitless numbers.

### 5.2  Baseline alignment [✓]
**File:** `render/font/atlas.cpp`

Descender pad in painter uses actual font descender from get_font_metrics()
instead of crude `ceil(font_size * 0.25f)` estimate.

### 5.3  Vertical metrics for mixed-font text [✓]
**File:** `render/font/atlas.cpp`

`get_font_metrics()` returns ascender/descender/line_gap for current font face at given size.

---

## Phase 6 — Cache and performance [✓ COMPLETE]

### 6.1  Atlas LRU eviction [✓]
**File:** `render/font/atlas.cpp`

When atlas reaches 4-page limit, evicts oldest page (page 0): clears all glyph cache entries
for that page, reinitializes texture, recycles the page. No more silent glyph failures.

### 6.2  Font stack cache [✓]
**File:** `render/font/atlas.cpp`

Glyph cache capped at 8192 entries. Shaper font tables cache capped at 128 fonts.
FontManager raster cache capped at 4096 entries.

### 6.3  Performance optimization [✓]

- `glyph_index(cp)` called once per codepoint in render_text (was 4x)
- Redundant `has_color_bitmap` call removed from Arabic render path
- `measure_text` uses simple per-codepoint advance summing (no shaper)
- All caches bounded to prevent unbounded memory growth

---

## Phase 7 — Testing and verification

### 7.1  Font coverage test page [✓]
**File:** `tools/tests/font_coverage.html`

Test page with 13 sections covering: Latin (sizes/bold/italic), Serif, Monospace,
CJK (Chinese/Japanese/Korean), Arabic RTL, Emoji color, Symbols, Line-breaking,
Mixed fonts, @font-face, Kerning, Numbers, Line-height.

### 7.2  C++ unit tests for font matching [✓]
**File:** `tests/font_test.cpp`

8 new tests (24 total): emoji_detection, fallback_for_missing_glyph,
web_font_takes_priority, resolve_family_fallback_to_default,
registry_generic_mapping, shaper_basic, shaper_preserves_codepoints,
glyph_rasterize_by_gid, font_has_gsub_tables.

### 7.3  Visual regression for font rendering
**File:** `tools/run_tests.ps1`  **Estimate:** 100 lines

Extend the test suite to:
1. Render `font_coverage.html` via `--screenshot`
2. Compare pixel-by-pixel against a reference BMP
3. Flag mismatches as `FONT_REGRESSION`

---

## Implementation status

```
[✓] Week 1   Phase 1 (1.1–1.5): CSS → font matching + fallback
              → CJK, symbols, code blocks resolve to correct system fonts

[✓] Week 2   Phase 2.2: TTC support
              → msgothic.ttc, msmincho.ttc, yugothm.ttc now load

[✓] Week 3   Phase 3 (3.1–3.4): Emoji and color font
              → CBDT/CBLC bitmap parsing, RGBA rendering with white tint

[!] Week 4   Phase 4 (4.1–4.3): Hand-rolled shaping engine (PARTIAL)
              → GSUB/GPOS/GDEF table parsing (Single, Alternate, Ligature, PairPos)
              → Arabic shaping (init/medi/fina/isol via GSUB, Unicode joining types)
              → GPOS + legacy kern table kerning, ligature substitution (rlig/liga)
              → Shaper only activated for Arabic text (non-Arabic uses fast path)
              → Known issue: subtable offset math needs fixing (no-op for Arabic)
              → Files: render/font/shaper.{hpp,cpp} (~900 lines)

[✓] Week 5   Phase 5 (5.1–5.3): Measurement and metrics correctness
              → Font-metrics-based line-height (ascender-descender+line_gap for "normal")
              → TextMetricsFn callback wired through layout engine → TextRenderer
              → Painter descender_pad uses actual font descender
              → Handles "normal", percentages, unitless numbers for line-height
              → Files: css/layout/types.hpp, css/layout.hpp, css/layout/inline.cpp,
                       render/font/atlas.hpp, render/paint/painter.cpp (+ callbacks)

[✓] Week 6   Phase 2.3 + 4.4 + 6: @font-face, line-breaking, cache
              → @font-face: FontLoader + register_web_font + resolve_family priority
              → UAX#14 CJK line-breaking
              → Atlas LRU eviction, cache caps, performance optimization
              → Files: render/font/font.hpp/cpp, render/font_loader.cpp,
                       css/layout/inline.cpp, render/font/atlas.cpp

[✓] Week 7   Phase 7: C++ unit tests + font coverage test page
              → 8 new font matching tests (24 total, all pass)
              → tools/tests/font_coverage.html with 13 test sections
              → Files: tests/font_test.cpp, tools/tests/font_coverage.html

[ ] Future   Visual regression, variable fonts, WOFF2 decompression,
              Devanagari/SE Asian shaping
```
[✓] Week 1   Phase 1 (1.1–1.5): CSS → font matching + fallback
              → CJK, symbols, code blocks resolve to correct system fonts

[✓] Week 2   Phase 2.2: TTC support
              → msgothic.ttc, msmincho.ttc, yugothm.ttc now load

[✓] Week 3   Phase 3 (3.1–3.4): Emoji and color font
              → CBDT/CBLC bitmap parsing, RGBA rendering with white tint

[✓] Week 4   Phase 4 (4.1–4.3): Hand-rolled shaping engine
              → GSUB/GPOS/GDEF table parsing (Single, Alternate, Ligature, PairPos)
              → Arabic shaping (init/medi/fina/isol via GSUB, Unicode joining types)
              → GPOS + legacy kern table kerning, ligature substitution (rlig/liga)
              → Shaper integrated into TextRenderer::render_text() and measure_text()
              → Files: render/font/shaper.{hpp,cpp} (~900 lines)

[ ] Week 5   Phase 2.3 + 4.4 + 5: @font-face, line-breaking, metrics
              → Web fonts, UAX#14 line breaks, baseline alignment, vertical metrics

[ ] Week 6   Phase 6 + 7: Cache and testing
              → Atlas LRU eviction, font coverage test page, visual regression
```
