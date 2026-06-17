# Browser Engine Test Harness

Automated testing and regression harness for the from-scratch C++ browser engine.
Compares engine output against spec-compliant reference parsers at every pipeline
stage: HTML parsing → CSS parsing → cascade → layout → paint.

## Architecture

```
test.html ──┬──▶ reference_html.js ──▶ expected-dom.json ──┐
             │                                               │
             └──▶ engine --dump-dom  ──▶ actual-dom.json ──┐│
                                                             ▼│
                                                      json_diff.py
                                                             │
                                               PASS / FAIL / MINOR
```

The harness has four components:

| Component | Location | Purpose |
|-----------|----------|---------|
| Reference scripts | `tools/reference_html.js`, `tools/reference_css.js`, `tools/reference_js.js` | Parse input using spec-compliant parsers (jsdom, postcss, acorn) |
| Engine dump modes | `src/main.cpp` (CLI flags) | Run individual pipeline stages via `--dump-dom`, `--dump-css`, `--dump-cascade`, `--dump-layout`, `--dump-display-list` |
| Diff engine | `tools/json_diff.py` | Recursively compare expected vs actual JSON trees |
| Test runner | `tools/run_tests.sh` | Orchestrate everything, collect results |

## Prerequisites

- **Node.js** >= 18 (for reference scripts)
- **Python 3** >= 3.8 (for diff engine)
- **C++ compiler** with C++20 coroutine support (MSVC 2022 recommended)
- **CMake** >= 3.16 (for building the engine)

## Setup

```bash
# 1. Build the browser engine
cd browser
cmake -S . -B build
cmake --build build

# 2. Install Node.js dependencies for reference scripts
cd tools
npm install
cd ..

# 3. Verify everything works
tools/run_tests.sh
```

## Running Tests

### Full test suite

```bash
# Uses the default build/browser.exe
tools/run_tests.sh

# Or specify a custom browser binary
BROWSER=/path/to/browser.exe tools/run_tests.sh
```

### Full test suite with JSON output

```bash
tools/run_tests.sh --json
```

This writes structured JSON results to `tools/results/latest_run.json` with
per-test diff entries, timing, and root-cause analysis.

### Single test

```bash
# Run a single test through all applicable pipeline stages
BROWSER=build/browser.exe tools/json_diff.py \
  tools/tests/my_test.expected-dom.json \
  tools/tests/my_test.actual-dom.json \
  --mode dom
```

### Diff with JSON output

```bash
tools/json_diff.py expected.json actual.json --mode dom --json-out diffs.json
```

## Pipeline Stage Reference

### DOM dump (`--dump-dom <file>`)

Parses HTML and outputs the full DOM tree as JSON. Schema:

```json
{
  "type": "element",
  "tag": "div",
  "attributes": { "class": "foo", "id": "bar" },
  "children": [
    { "type": "text", "data": "Hello", "data_normalized": "Hello" },
    { "type": "comment", "data": "comment text" }
  ]
}
```

Expected DOM files are generated from the reference parser (jsdom).
These are the gold standard — any mismatch is a real parsing bug.

### CSS dump (`--dump-css <file>`)

Parses CSS and outputs the full AST as JSON. Schema:

```json
{
  "rules": [{ "selectors": ["div.foo"], "declarations": [...] }],
  "at_rules": [{ "name": "media", "prelude": "screen", "rules": [...] }]
}
```

Expected CSS files are generated from the reference parser (postcss).

### Cascade dump (`--dump-cascade <file>`)

Runs full HTML parse + inline CSS collection + cascade (UA + author + inline styles).
Outputs computed styles per element:

```json
{
  "elements": [{
    "tag": "body",
    "id": "",
    "classes": [],
    "computed": {
      "display": { "type": "KEYWORD", "value": "block" },
      "margin-left": { "type": "KEYWORD", "value": "auto" }
    }
  }]
}
```

**Expected cascade files are engine-bootstrapped snapshots**, not reference-parser
outputs. Neither jsdom nor postcss can compute the cascade. Generate them from
a known-good engine state:

```bash
for f in tools/tests/cascade_*.html; do
    stem="${f%.html}"
    "$BROWSER" --dump-cascade "$f" > "${stem}.expected-cascade.json"
done
```

### Layout dump (`--dump-layout <file>`)

Runs full pipeline through layout. Outputs layout tree:

```json
{
  "tag": "body",
  "is_text": false,
  "text": "",
  "content": { "x": 8.0, "y": 8.0, "width": 784.0, "height": 2400.0 },
  "margin": { "top": 8.0, "right": 8.0, "bottom": 8.0, "left": 8.0 },
  "padding": { "top": 0, "right": 0, "bottom": 0, "left": 0 },
  "border": { "top": 0, "right": 0, "bottom": 0, "left": 0 },
  "children": [...]
}
```

**Expected layout files are engine-bootstrapped snapshots**, not reference-parser
outputs. Generate them from a known-good engine state:

```bash
for f in tools/tests/layout_*.html tools/tests/regression_*.html; do
    stem="${f%.html}"
    "$BROWSER" --dump-layout "$f" > "${stem}.expected-layout.json"
done
```

### Display list dump (`--dump-display-list <file>`)

Runs full pipeline through paint. Outputs display commands:

```json
[
  { "cmd": "FILL_RECT", "x": 8.0, "y": 8.0, "w": 784.0, "h": 24.0, "color": "#ffffff" },
  { "cmd": "DRAW_TEXT", "x": 8.0, "y": 8.0, "w": 340.0, "h": 32.0, "text": "Hello", "font_size": 16.0 }
]
```

**Expected display-list files are engine-bootstrapped snapshots**, not reference-parser
outputs. Generate them from a known-good engine state:

```bash
for f in tools/tests/display_*.html; do
    stem="${f%.html}"
    "$BROWSER" --dump-display-list "$f" > "${stem}.expected-display-list.json"
done
```

## Output Interpretation

### Diff entries

Each diff entry has:

```
[SEVERITY] [CATEGORY] path.to.node
  expected: <value>
  actual:   <value>
```

**Severity:**
- `CRITICAL` — structural error, wrong value, missing/extra node (exit code 1)
- `MINOR` — whitespace difference, numeric tolerance, color channel drift (exit code 2)

**Categories:**
| Category | Meaning |
|----------|---------|
| `PARSE_ERROR` | Structural parsing mismatch |
| `CASCADE_ERROR` | Wrong computed style value or type |
| `LAYOUT_ERROR` | Wrong position, size, or geometry |
| `ENCODING_ERROR` | Multibyte Unicode garbled in output |
| `MISSING_NODE` | Expected node not present |
| `EXTRA_NODE` | Unexpected node present |
| `WRONG_VALUE` | Value mismatch |

### Exit codes

| Code | Meaning |
|------|---------|
| 0 | All checks pass |
| 1 | One or more CRITICAL failures |
| 2 | Only MINOR failures (no critical) |

## Adding a New Regression Test

1. Create a test HTML file in `tools/tests/regression_NNN_description.html`:

```html
<!DOCTYPE html>
<html>
<head><meta charset="utf-8">
<style>
  /* Minimal CSS to reproduce the bug */
</style>
</head>
<body>
  <!-- Content that triggers the bug -->
</body>
</html>
```

2. Generate expected DOM output:

```bash
node tools/reference_html.js tools/tests/regression_NNN_description.html > \
  tools/tests/regression_NNN_description.expected-dom.json
```

3. Generate expected cascade and layout from a known-good engine state:

```bash
BROWSER=build/browser.exe
"$BROWSER" --dump-cascade tools/tests/regression_NNN_description.html \
  > tools/tests/regression_NNN_description.expected-cascade.json
"$BROWSER" --dump-layout tools/tests/regression_NNN_description.html \
  > tools/tests/regression_NNN_description.expected-layout.json
```

4. Run the test suite to verify:

```bash
tools/run_tests.sh
```

The test runner automatically discovers any `.html` or `.css` file in the test
directory. No configuration changes needed.

## Adding Cascade and Layout Tests

Expected cascade and layout files cannot be generated from reference parsers.
They are **engine-bootstrapped snapshots**. Follow this workflow:

1. Ensure the engine is in a known-good state (all existing tests pass).
2. Create the test HTML file.
3. Generate expected files from the engine.
4. Commit both the HTML file and the expected JSON files.
5. If a future change breaks the test, investigate whether the engine change
   or the snapshot is wrong.

## CI Integration

### GitHub Actions

```yaml
- name: Build
  run: |
    cmake -S . -B build
    cmake --build build

- name: Test
  run: tools/run_tests.sh
  env:
    BROWSER: build/browser.exe
```

### JSON CI output

```yaml
- name: Test
  run: tools/run_tests.sh --json
```

The JSON output at `tools/results/latest_run.json` can be parsed by CI tools
for structured reporting. It includes root-cause analysis — if a DOM failure
is detected, cascade/layout/display failures in the same test are tagged as
`secondary`, enabling immediate identification of the first broken stage.

### Expected outcomes

- **0 failures**: CI passes green
- **CRITICAL failures only**: CI fails red — a real bug has been introduced
- **MINOR failures only**: CI may be configured to pass with warning (threshold configurable)

### History tracking

Run results are appended to `tools/results/history.txt`:

```
2026-06-17 14:30:22 | layout_flexbox.html | dom=PASS css=- cascade=SKIP layout=FAIL disp=- | FAIL
2026-06-17 14:30:22 | html_utf8.html | dom=PASS css=- cascade=PASS layout=PASS disp=- | PASS
```

Each line tracks a single test in a single run. Use `grep` to check flakiness:

```bash
grep "layout_flexbox" tools/results/history.txt
```

## Determinism

The harness is fully deterministic:
- Identical input → identical reference output (jsdom/postcss are deterministic)
- Identical input → identical engine output (all PRNG eliminated from headless modes)
- Diff engine is a pure function of its two JSON inputs

## Test File Conventions

- **HTML test files**: `tools/tests/*.html` — run through DOM, cascade, layout, and display-list modes
- **CSS test files**: `tools/tests/*.css` — run through CSS mode only
- **Expected outputs**: `tools/tests/*.expected-{dom,css,cascade,layout,display-list}.json`
- **Actual outputs**: `tools/tests/*.actual-{dom,css,cascade,layout,display-list}.json` (generated at test time)
- **Results**: `tools/results/latest_run.txt` (full diff of failures)
- **Results (JSON)**: `tools/results/latest_run.json` (structured JSON with `--json` flag)
- **History**: `tools/results/history.txt` (per-test-per-run lines with timestamps)

## `--json-out` flag for `json_diff.py`

When passed, writes a JSON array of diff entry objects to the specified file:

```bash
json_diff.py expected.json actual.json --mode dom --json-out diffs.json
```

Each entry has: `path`, `expected`, `actual`, `severity`, `category`.

## Full Test File List by Category

### HTML Parsing Tests (23 files)
| File | Tests |
|------|-------|
| `html_basic_structure.html` | Fundamental document structure |
| `html_optional_closing_tags.html` | Auto-closing for li, p, dt, dd, td, tr |
| `html_void_elements.html` | All void elements don't consume children |
| `html_mixed_case.html` | Tag/attribute name normalization to lowercase |
| `html_entities.html` | Named and numeric entity decoding |
| `html_utf8.html` | Multibyte UTF-8 (Japanese, Arabic, emoji, CJK) |
| `html_deep_nesting.html` | 20+ nested div levels |
| `html_attributes.html` | Boolean, data, ARIA, special chars, Unicode attrs |
| `html_comments.html` | Comments including edge cases |
| `html_script_style.html` | Script/style content including </script> in literal |
| `html_table.html` | Full table with thead, tbody, tfoot, colspan, rowspan |
| `html_forms.html` | All form elements and input types |
| `html_media.html` | Img, picture, video, audio elements |
| `html_svg_inline.html` | Inline SVG elements |
| `html_malformed.html` | Error recovery (unclosed, misnested, stray tags) |
| `html_template_element.html` | Template element content in document fragment |
| `html_adoption_agency.html` | Misnested formatting elements |
| `html_foster_parenting.html` | Table foster parenting algorithm |
| `html_noscript.html` | Noscript element in head/body |
| `html_base_tag.html` | Base href attribute |
| `html_boolean_attributes.html` | All boolean attributes |
| `html_whitespace_normalization.html` | Whitespace collapse and pre preservation |
| `html_cdata_svg.html` | CDATA in inline SVG |

### CSS Parsing Tests (11 files)
| File | Tests |
|------|-------|
| `css_selectors.css` | Every selector type and combinator |
| `css_values.css` | Every value type (keywords, lengths, colors, calc) |
| `css_shorthands.css` | All shorthand properties with expansion |
| `css_atrules.css` | All at-rules with nesting |
| `css_custom_properties.css` | Custom properties, var(), inheritance |
| `css_specificity.css` | Specificity ordering and !important |
| `css_pseudo_elements.css` | ::before, ::after, ::first-line, ::first-letter |
| `css_logical_properties.css` | margin-inline, padding-block, inset, etc. |
| `css_env_function.css` | env() with safe-area-inset fallbacks |
| `css_has_pseudo.css` | :has() selector with various arguments |
| `css_layer_specificity.css` | @layer ordering and specificity |

### Cascade Tests (12 files)
| File | Tests |
|------|-------|
| `cascade_inheritance.html` | Inherited vs non-inherited properties |
| `cascade_specificity.html` | Tag vs class vs ID vs inline priority |
| `cascade_important.html` | !important at author and inline levels |
| `cascade_shorthand_expansion.html` | Margin/padding/border expansion, auto keyword |
| `cascade_custom_properties.html` | var(), :root variables, overrides, fallbacks |
| `cascade_ua_vs_author.html` | Author rules override UA defaults |
| `cascade_initial_unset_revert.html` | initial, unset, revert keywords |
| `cascade_animation_override.html` | Animation property precedence |
| `cascade_layer_order.html` | @layer ordering and !important in layers |
| `cascade_custom_property_cycle.html` | var() circular references |

### Layout Tests (14 files)
| File | Tests |
|------|-------|
| `layout_block_model.html` | Width, box-sizing, margin auto, padding, border |
| `layout_margin_collapse.html` | Sibling/parent-child margin collapse |
| `layout_inline.html` | Inline elements, line-height, text-align |
| `layout_positioning.html` | Relative, absolute, fixed, sticky, z-index |
| `layout_flexbox.html` | All flexbox properties |
| `layout_grid.html` | Grid with fr, repeat, minmax, areas |
| `layout_overflow.html` | Clipping and scroll containers |
| `layout_float.html` | Float and clear |
| `layout_table.html` | CSS table display values |
| `layout_multicolumn.html` | Multicolumn layout |
| `layout_writing_mode.html` | Vertical writing modes |
| `layout_min_max_content.html` | Min-content, max-content, fit-content |
| `layout_aspect_ratio.html` | Aspect ratio sizing |
| `layout_calc_in_layout.html` | calc() in layout properties |

### Display List Tests (6 files)
| File | Tests |
|------|-------|
| `display_fill_rect.html` | FILL_RECT commands |
| `display_draw_text.html` | DRAW_TEXT commands |
| `display_border.html` | Border geometry in display list |
| `display_background_color.html` | Background color fill commands |
| `display_stacking_order.html` | z-index command ordering |
| `display_opacity.html` | Opacity affect on commands |

### Regression Tests (10 files)
| File | Tests |
|------|-------|
| `regression_001_utf8_middot.html` | UTF-8 middle dot not corrupted |
| `regression_002_margin_auto_coercion.html` | auto keyword preserved in cascade |
| `regression_003_inline_block_width.html` | Inline element not full-width |
| `regression_004_anchor_border.html` | No spurious border on anchors |
| `regression_005_content_y_padding.html` | Child content.y includes parent border+padding |
| `regression_006_flex_grow_sum.html` | Flex-grow proportional distribution |
| `regression_007_absolute_out_of_flow.html` | Absolute child out of parent flow |
| `regression_008_margin_collapse_adjacent.html` | Adjacent sibling margin collapse |
| `regression_009_inline_whitespace.html` | Whitespace collapse between inline elements |
| `regression_010_table_cell_width.html` | Even cell width distribution |
