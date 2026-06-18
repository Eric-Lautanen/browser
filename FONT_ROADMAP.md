# Font System Roadmap

## Current State

CSS `font-family` is now fully wired through the render pipeline:
- `FontRegistry` enumerates Windows fonts via `HKLM\...\Fonts` at startup
- `FontManager::resolve_family()` maps CSS family names → physical `FontFace*`, including CSS generics (`sans-serif`→Arial, etc.)
- `PaintCommand::font_family` is set by the painter and passed through the executor to `TextRenderer::render_text()`
- Per-character fallback walks the fallback chain when the primary face lacks a glyph
- TTC font collections (`msgothic.ttc`, `msmincho.ttc`, `yugothm.ttc`) load correctly via `load_from_ttc_memory()`
- CBDT/CBLC color bitmap tables are parsed for emoji rendering (Segoe UI Emoji)
- Color emoji render as RGBA quad with white tint (preserving natural glyph colors)

**Still missing:** Complex text shaping (Arabic, SE Asian), OpenType GSUB/GPOS (ligatures, kerning pairs), `@font-face` web fonts, variable fonts, atlas LRU eviction, proper line-height from font metrics.

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

### 2.3  @font-face support
**File:** `html/resource_loader.cpp`, `css/cascade/engine.cpp`, `render/font/registry.cpp` **Estimate:** 150 lines

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

## Phase 3 — Emoji and color font support

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

### 4.1  Ligature-aware text measurement
**File:** `render/font/atlas.cpp`  **Estimate:** 80 lines

Currently `measure_text` and `render_text` process codepoints independently.
Common ligatures (fi, fl, ffi, ffl, —) use single glyphs in the font but the
code doesn't check for them.

1. After decoding each codepoint, check the font's GSUB table for ligature substitutions
2. If a sequence like {'f', 'i'} maps to a single ligature glyph, consume both codepoints
3. Use the ligature glyph's advance width, not the sum of individual advances

**Alternative (simpler):** Hard-code common ligatures and check `glyph_index` for the
combined glyph first. Skip if the font doesn't have it.

### 4.2  Kerning for all font pairs
**File:** `render/font/font.cpp`  **Estimate:** 40 lines

Currently kerning is read from the `kern` table (TrueType format only).
OpenType fonts use GPOS instead. Add GPOS `PairPos` subtable parsing
for kerning lookups.

### 4.3  Script-specific shaping stubs
**File:** `render/font/shaper.{hpp,cpp}`  **Estimate:** 150 lines

Without HarfBuzz, complex scripts need manual minimum shaping:

- **Arabic:** character forms change based on position (isolated/initial/medial/final).
  Map each Arabic codepoint to the correct glyph via the font's `init`/`medi`/`fina`
  GSUB features. Without this, Arabic renders as isolated forms only (legible but wrong).
- **Devanagari / SE Asian scripts:** need reordering + conjunct shaping.
  These are very complex — document as known limitation (use fallback to isolated forms).

### 4.4  Line-breaking by Unicode rules (UAX #14)
**File:** `css/layout/inline.cpp`  **Estimate:** 100 lines

Currently line-breaking is done at whitespace or soft-hyphen only.
UAX #14 defines break opportunities for CJK (allow break after every character),
hyphenation points, and non-breaking contexts.

---

## Phase 5 — Measurement and metrics correctness

### 5.1  Use font metrics for line-height
**File:** `css/layout/block.cpp`, `render/font/atlas.cpp`  **Estimate:** 60 lines

`line-height` currently uses a hard-coded multiplier. Should use the font's
ascender + descender + line_gap (scaled by `line-height` multiplier).

### 5.2  Baseline alignment
**File:** `render/font/atlas.cpp`  **Estimate:** 30 lines

`render_text` currently positions glyphs using `ascender - bearing_y`.
This is close but not pixel-perfect across fonts. Fix baseline alignment
to use the font's baseline-to-ascender ratio consistently.

### 5.3  Vertical metrics for mixed-font text
**File:** `render/font/atlas.cpp`  **Estimate:** 40 lines

When fallback kicks in mid-string (e.g., `"Hello 世界"`), the CJK font has
different ascender/descender values. The line height should be the maximum
of all fonts used on the line, not just the primary font.

---

## Phase 6 — Cache and performance

### 6.1  Atlas LRU eviction
**File:** `render/font/atlas.cpp`  **Estimate:** 80 lines

Current atlas wraps at 4 pages, then new glyphs silently fail. Add LRU eviction:
track last-use time per glyph, evict least-recently-used when a new page is needed.
Re-rasterize evicted glyphs on demand.

### 6.2  Font stack cache
**File:** `render/font/registry.cpp`  **Estimate:** 30 lines

Cache resolved font stacks (CSS string → `std::vector<FontFace*>`).
Invalidate on registry change (new font downloaded via @font-face).

### 6.3  Glyph fallback cache sizing
**File:** `render/font/atlas.cpp`  **Estimate:** 20 lines

The fallback cache currently has no size limit. Cap at 4096 entries, LRU eviction.

---

## Phase 7 — Testing and verification

### 7.1  Font coverage test page
**File:** `tools/tests/font_coverage.html`  **Estimate:** n/a

Create a test page with:
- Latin text at various sizes (already works)
- CJK chars (hanzi, hiragana, katakana)
- Arabic (RTL shaping)
- Emoji sequences
- Common symbols (→ • © ™ — …)
- @font-face web font test
- Monospace code block test

### 7.2  Visual regression for font rendering
**File:** `tools/run_tests.ps1`  **Estimate:** 100 lines

Extend the test suite to:
1. Render `font_coverage.html` via `--screenshot`
2. Compare pixel-by-pixel against a reference BMP
3. Flag mismatches as `FONT_REGRESSION`

### 7.3  C++ unit tests for font matching
**File:** `tests/font_test.cpp`  **Estimate:** 80 lines

- Registry lookup by family name
- Generic family mapping
- Font stack parsing and resolution
- Per-character fallback
- Emoji detection

---

## Implementation status

```
[✓] Week 1   Phase 1 (1.1–1.5): CSS → font matching + fallback
             → CJK, symbols, code blocks resolve to correct system fonts

[✓] Week 2   Phase 2.2: TTC support
             → msgothic.ttc, msmincho.ttc, yugothm.ttc now load

[✓] Week 3   Phase 3 (3.1–3.4): Emoji and color font
             → CBDT/CBLC bitmap parsing, RGBA rendering with white tint

[ ] Week 4   Phase 2.3 + 4 (GSUB/GPOS ligatures, kerning, @font-face)
             → fi/fl ligatures, web fonts, GPOS kerning pairs

[ ] Week 5   Phase 4.3–4.4 + 5: Shaping, line-breaking, metrics
             → Arabic/SE Asian shaping, UAX#14 line breaks, baseline alignment

[ ] Week 6   Phase 6 + 7: Cache and testing
             → Atlas LRU eviction, font coverage test page, visual regression
```
