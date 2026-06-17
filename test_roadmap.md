# Test Roadmap — Root `index.html` Coverage

> One testable feature per line. Run each with `.\tools\run_tests.ps1 -filter "<name>"`.
> Fix features in order — downstream stages depend on upstream ones.

---

## Phase 1: HTML Parsing (DOM Stage)

### Core Structure
- [x] **`index`** — Full comprehensive test page: DOCTYPE, html, head, body, meta charset, viewport, title, link favicon
- [x] **`html_basic_structure.html`** — Fundamental document with html/head/body

### Headings
- [x] `index.html` `<h1>` through `<h6>` all six heading levels

### Text & Inline Elements
- [x] `index.html` `<strong>`, `<em>`, `<code>`, `<a>` inline elements
- [x] **`html_text_formatting.html`** — `<u>`, `<s>`, `<sub>`, `<sup>`, `<mark>` formatting
- [x] **`html_semantic_inline.html`** — `<abbr>`, `<kbd>`, `<var>`, `<samp>`, `<cite>`, `<time>`
- [x] **`html_entities.html`** — HTML entities: `&amp;`, `&lt;`, `&gt;`, `&quot;`, `&copy;`, `&mdash;`, `&nbsp;`
- [ ] `index.html` Non-breaking space `&nbsp;` rendered inline

### Media
- [x] `index.html` `<img>` with src, alt, width, height attributes
- [x] **`html_semantic_structural.html`** — `<figure>` / `<figcaption>` container
- [ ] `index.html` `<picture>` / `<source>` with `srcset` / `media` attributes
- [x] **`html_svg_inline.html`** — Inline `<svg>` with `<rect>`, `<circle>`, `<polygon>`, `<text>`
- [x] `index.html` `<link>` with `data:` URI href
- [x] **`html_media.html`** — `<audio>` / `<video>` elements

### Lists
- [x] `index.html` Nested `<ul>` / `<li>` unordered lists
- [x] `index.html` Nested `<ol>` / `<li>` ordered lists
- [x] **`html_definition_list.html`** — `<dl>`, `<dt>`, `<dd>` definition lists

### Tables
- [x] **`html_table.html`** — `<table>` with `<thead>`, `<tbody>`, `<tfoot>`
- [x] `index.html` `<th>` with `rowspan` attribute
- [x] `index.html` `<th>` with `colspan` attribute
- [x] `index.html` `<tfoot>` with `colspan`

### Forms
- [x] **`html_forms.html`** — `<form>` with `action`, `method` attributes
- [x] `index.html` `<input type="text">` with placeholder
- [x] `index.html` `<input type="password">`
- [x] `index.html` `<input type="number">` with min/max/value
- [x] `index.html` `<textarea>` with rows/cols/default text
- [x] `index.html` `<input type="checkbox">` with `checked` attribute
- [x] `index.html` `<input type="radio">` with `checked` + name grouping
- [x] `index.html` `<select>` / `<option>` with `selected` attribute
- [x] `index.html` `<input type="email">` with placeholder
- [x] `index.html` `<input type="search">` with placeholder
- [x] `index.html` `<input type="date">` with value
- [x] `index.html` `<input type="range">` with min/max/value
- [x] `index.html` `<input type="color">` with value
- [x] **`html_datalist_progress_meter.html`** — `<datalist>` with options
- [x] **`html_datalist_progress_meter.html`** — `<progress>` with value/max
- [x] **`html_datalist_progress_meter.html`** — `<meter>` with low/high/optimum
- [x] `index.html` `<button type="submit">`, `<button type="reset">`
- [x] `index.html` `<label>` with `for` attribute
- [x] `index.html` `data-*` attributes (`data-testid`, `data-custom-attr`)

### Semantic / Structural
- [x] **`html_semantic_structural.html`** — `<nav>`, `<main>`, `<article>`, `<section>`, `<aside>`
- [x] `index.html` `<article>` with nested `<header>` and `<footer>`
- [x] `index.html` `<section>` element
- [x] `index.html` `<aside>` element
- [x] **`html_semantic_structural.html`** — `<figure>` / `<figcaption>`
- [x] **`html_details_summary.html`** — `<details>` / `<summary>` elements
- [x] **`html_details_summary.html`** — `<details open>` attribute
- [x] `index.html` `<blockquote>` element
- [x] `index.html` `<figure>` / `<figcaption>` in integration context
- [x] `index.html` `<pre>` / `<code>` block
- [x] `index.html` `<hr>` horizontal rule

### Attributes
- [x] **`html_attributes.html`** — `id` attribute on headings (e.g., `id="headings"`)
- [x] **`html_attributes.html`** — `class` attribute on elements
- [x] **`html_tabindex_aria.html`** — `tabindex="0"` and `tabindex="-1"`
- [x] **`html_tabindex_aria.html`** — `role` attribute (ARIA)
- [x] **`html_tabindex_aria.html`** — `aria-label` attribute
- [x] **`html_tabindex_aria.html`** — `aria-hidden` attribute
- [x] **`html_tabindex_aria.html`** — `lang` attribute (`lang="fr"`, `lang="ja"`, `lang="ar"`)
- [x] **`html_tabindex_aria.html`** — `dir` attribute (`dir="rtl"`)
- [x] **`html_attributes.html`** — `lang="ja"` + `data-label="日本語"` on same element
- [x] **`html_boolean_attributes.html`** — boolean attributes (`checked`, `disabled`, `required`, etc.)
- [x] **`html_base_tag.html`** — `<base>` tag with href

### Parser Stress
- [x] **`html_optional_closing_tags.html`** — Optional closing tags (`<li>` without `</li>`, `<p>` without `</p>`)
- [x] **`html_mixed_case.html`** — Mixed-case tag names (`<P>`, `<DIV>`, `<SPAN>`, `<CODE>`)
- [x] **`html_deep_nesting.html`** — Deep nesting (5 levels of `<div>`)
- [x] **`html_utf8.html`** — Unicode content (Japanese, Arabic, emoji)
- [x] **`html_comments.html`** — Comment edge cases (empty comment, double-dash via entity)
- [x] **`html_void_elements.html`** — Void elements: `<br>`, `<hr>`, `<img>`, `<input>`, `<meta>`, `<link>`
- [x] **`html_entities.html`** — HTML entities and character references
- [x] **`html_whitespace_normalization.html`** — Whitespace collapse (multiple spaces in text)
- [x] **`html_script_style.html`** — `<script>` and `<style>` raw text content

### Still Failing (8)
- [ ] **`html_adoption_agency.html`** — Adoption agency algorithm for misnested formatting elements
- [ ] **`html_cdata_svg.html`** — CDATA sections inside SVG
- [ ] **`html_foster_parenting.html`** — Foster parenting algorithm (table text insertion)
- [ ] **`html_malformed.html`** — Malformed/invalid HTML edge cases
- [ ] **`html_noscript.html`** — `<noscript>` element handling
- [ ] **`html_template_element.html`** — `<template>` element with DocumentFragment

---

## Phase 2: CSS Parsing (CSS Stage)

### Selectors
- [x] **`css_selectors.css`** — Universal, type, class, descendant, pseudo-class selectors
- [x] `index.html` Universal selector `*` with `::before`, `::after`
- [x] `index.html` `:root` pseudo-class
- [x] `index.html` Type selectors (`h1`, `p`, `a`, `code`, `pre`, `table`, etc.)
- [x] `index.html` Class selectors (`.box`, `.flex-container`, `.grid-item`, etc.)
- [x] `index.html` Descendant combinator (`pre code`, `ul li`, etc.)
- [x] `index.html` `:first-child` pseudo-class
- [x] `index.html` `:last-child` pseudo-class
- [x] `index.html` `:nth-child(even)` pseudo-class
- [x] `index.html` `:not(.excluded)` negation pseudo-class
- [x] `index.html` `:hover` pseudo-class (interactive)
- [x] `index.html` `:focus` pseudo-class (interactive)
- [x] `index.html` `:visited` pseudo-class

### Pseudo-elements
- [x] **`css_pseudo_first_line_letter.html`** — `::first-line` pseudo-element
- [x] **`css_pseudo_first_line_letter.html`** — `::first-letter` pseudo-element (with float, font-size, color)
- [x] `index.html` `::before` with `content` property
- [x] `index.html` `::after` with `content` property

### At-Rules / Media Queries
- [x] **`css_media_print.html`** — `@media print` with `display: none`
- [x] `index.html` `@media (max-width: 600px)` with grid/flex changes
- [x] `index.html` `@media (prefers-color-scheme: dark)` with `::after` content

### CSS Values & Functions
- [x] **`css_values.css`** — CSS values, colors, units, functions
- [x] `index.html` CSS custom properties (`--color-primary`, `--color-accent`, etc.)
- [x] `index.html` `var()` function (`var(--color-primary)`, `var(--spacing-md)`)
- [x] `index.html` `calc(100% - 4em)` function in width
- [x] **`layout_visibility_clamp.html`** — `clamp(0.85rem, 2vw, 1.25rem)` in font-size
- [x] `index.html` `rgba()` color function (`.z-on-top` background)
- [x] `index.html` `url('data:...')` for favicon

---

## Phase 3: Cascade (Cascade Stage)

### Style Resolution
- [x] **`cascade_ua_vs_author.html`** — UA stylesheet vs author stylesheet precedence
- [x] **`cascade_important.html`** — `!important` override behavior
- [x] **`cascade_specificity.html`** — Selector specificity ordering
- [x] **`cascade_inheritance.html`** — Inherited vs non-inherited properties
- [x] **`cascade_initial_unset_revert.html`** — `initial` / `unset` / `revert` keywords
- [x] **`cascade_shorthand_expansion.html`** — Shorthand property expansion
- [x] **`cascade_custom_properties.html`** — Custom property resolution with `var()`
- [x] **`cascade_custom_property_cycle.html`** — Custom property cycle detection
- [x] **`cascade_layer_order.html`** — `@layer` cascade order
- [x] **`cascade_animation_override.html`** — Animation style override
- [x] `index.html` UA stylesheet: block display for body, h1-h6, p, etc.
- [x] `index.html` Inline `style` attribute overrides `<style>` block
- [x] `index.html` `var(--color-primary)` resolves to `#005fcc` in cascade
- [x] `index.html` `var(--spacing-md)` resolves to `1em` in padding
- [x] `index.html` `var(--radius)` resolves to `4px` in border-radius
- [x] `index.html` `var(--color-accent)` resolves to `#e63946` in hover color
- [x] `index.html` `display: none` on `.display-none`
- [x] `index.html` `display: inline-block` on `.display-inline-block`
- [x] **`layout_visibility_clamp.html`** — `visibility: hidden` computed style
- [x] **`layout_visibility_clamp.html`** — `visibility: visible` computed style

---

## Phase 4: Layout (Layout Stage)

### Box Model
- [x] **`layout_block_model.html`** — Margin, border, padding, content box model
- [x] `index.html` Body margin: `2em auto`, max-width: `800px`
- [x] `index.html` `box-sizing: border-box` on all elements
- [x] `index.html` H1-H6 font-size cascade (2em, 1.5em, 1.2em)
- [x] `index.html` `<hr>` height from border-top
- [x] `index.html` Inline `<code>` background/padding

### Block Layout
- [x] **`layout_css_features.html`** — Various block-level CSS features
- [x] `index.html` Block stacking: h1, h2, p, ul, ol, table, form, etc.
- [x] `index.html` `<blockquote>` border-left, padding-left, margin-left: 0

### Inline Layout
- [x] **`layout_inline.html`** — Inline element layout, alignment, spacing
- [x] `index.html` `<strong>` inline, `<em>` inline, `<code>` inline
- [x] **`html_text_formatting.html`** — `<u>` inline, `<s>` inline, `<sub>` inline, `<sup>` inline, `<mark>` inline
- [x] `index.html` `<abbr>`, `<kbd>`, `<var>`, `<samp>`, `<cite>` inline flow
- [x] `index.html` `<img>` inline replaced element
- [x] `index.html` `<br>` line break within `<p>`

### Lists
- [x] `index.html` Nested `<ul>` indentation
- [x] `index.html` Nested `<ol>` indentation
- [x] **`html_definition_list.html`** — `<dl>` block, `<dt>` block, `<dd>` indented
- [x] `index.html` `<li>` with nested `<ul>` as child

### Tables
- [x] **`layout_table.html`** — Table layout with header/body/footer, cell spanning
- [x] `index.html` Table: `<thead>`, `<tbody>`, `<tfoot>` display types
- [x] `index.html` Table: `colspan` and `rowspan` cell spanning
- [x] `index.html` `<th>` centered and bold
- [x] `index.html` `<td>` left-aligned

### Forms
- [x] `index.html` `<form>` as block with margin
- [x] `index.html` `<input>` inline-block replaced elements
- [x] `index.html` `<textarea>` inline-block with rows/cols sizing
- [x] `index.html` `<select>` inline-block with options
- [x] `index.html` `<button>` inline-block with text
- [x] **`html_datalist_progress_meter.html`** — `<progress>` inline-block
- [x] **`html_datalist_progress_meter.html`** — `<meter>` inline-block

### Positioning
- [x] **`layout_positioning.html`** — Relative, absolute, fixed, sticky positioning
- [x] `index.html` `position: relative` on `.pos-container`
- [x] `index.html` `position: absolute` on `.pos-absolute` (top, right)
- [x] `index.html` `position: sticky` with `top: 0`
- [x] `index.html` `z-index: 1` vs `z-index: 10` stacking

### Overflow
- [x] **`layout_overflow.html`** — Visible, hidden, scroll, auto overflow modes
- [x] `index.html` `overflow: hidden` clipping content
- [x] `index.html` `overflow: scroll` creating scroll container
- [x] `index.html` `overflow: auto` conditional scroll

### Flexbox
- [x] **`layout_flexbox.html`** — Flex container, items, grow/shrink/wrap/alignment
- [x] `index.html` `display: flex` on `.flex-container`
- [x] `index.html` `flex-wrap: wrap` wrapping children
- [x] `index.html` `gap: 0.5em` spacing between flex items
- [x] `index.html` `align-items: center` cross-axis alignment
- [x] `index.html` `flex: 1 1 80px` shorthand on `.flex-item`
- [x] `index.html` `flex-grow: 2` on `.flex-item.grow2`
- [x] `index.html` `flex-direction: column` on `.flex-col`
- [x] `index.html` `justify-content: space-between` alignment
- [x] `index.html` `align-items: flex-end` cross-axis end alignment
- [x] `index.html` `height: 60px`/`30px`/`50px`/`40px` flex item heights

### Grid
- [x] **`layout_grid.html`** — Grid container, tracks, areas, item placement
- [x] `index.html` `display: grid` on `.grid-container`
- [x] `index.html` `grid-template-columns: repeat(3, 1fr)` track sizing
- [x] `index.html` `gap: 0.5em` grid gutters
- [x] `index.html` `grid-column: span 2` item spanning
- [x] `index.html` `grid-template-areas` named areas
- [x] `index.html` `grid-area: header / sidebar / content / footer`

### Visibility / Opacity
- [x] `index.html` `display: none` element takes no space
- [x] **`layout_visibility_clamp.html`** — `visibility: hidden` preserves space
- [x] `index.html` `opacity: 0.5` on `.opacity-50`
- [x] **`layout_transition_transform.html`** — `transform: rotate(-2deg) translateX(8px)`
- [x] **`layout_transition_transform.html`** — `transition` property (parsed/layout, animation not required)
- [x] **`layout_visibility_clamp.html`** — `font-size: clamp(...)` resolves to valid length

### Calc
- [x] **`layout_calc_in_layout.html`** — `calc()` in width, height, margins
- [x] `index.html` `width: calc(100% - 4em)` resolves to container width minus 4em

### Float
- [x] **`layout_float.html`** — Floating elements, clear, text wrapping

### Margin Collapse
- [x] **`layout_margin_collapse.html`** — Adjacent/parent/empty block margin collapsing

### Min/Max Content
- [x] **`layout_min_max_content.html`** — `min-content`/`max-content` sizing

### Multi-column
- [x] **`layout_multicolumn.html`** — CSS multi-column layout

### Aspect Ratio
- [x] **`layout_aspect_ratio.html`** — `aspect-ratio` property sizing

### Writing Mode
- [x] **`layout_writing_mode.html`** — Vertical writing modes

### Semantic Elements Layout
- [x] **`html_semantic_structural.html`** — `<nav>` navigational block
- [x] **`html_semantic_structural.html`** — `<main>` main content block
- [x] **`html_semantic_structural.html`** — `<article>` self-contained content block
- [x] **`html_semantic_structural.html`** — `<section>` thematic grouping block
- [x] **`html_semantic_structural.html`** — `<aside>` sidebar block
- [x] **`html_semantic_structural.html`** — `<header>`/introductory block
- [x] **`html_semantic_structural.html`** — `<footer>` footer block
- [x] `index.html` `<details>` / `<summary>` elements
- [x] `index.html` Nested `<header>` inside `<article>`

### Inline SVG Layout
- [x] `index.html` `<svg>` inline replaced element with width/height
- [x] `index.html` SVG child elements: `<rect>`, `<circle>`, `<polygon>`, `<text>`

### Miscellaneous Layout
- [x] `index.html` Long unbroken word (`supercalifragilisticexpialidocious`) wrapping
- [x] `index.html` Whitespace collapse (multiple spaces → one)
- [x] `index.html` `<small>` inline with smaller font
- [x] `index.html` `<sup>` / `<sub>` vertical alignment
- [x] `index.html` `tabindex="0"` span is still inline

---

## Phase 5: Display List / Paint (Display List Stage)

### Background & Border
- [x] **`display_background_color.html`** — Background color rendering
- [x] **`display_fill_rect.html`** — Filled rectangle paint command
- [x] **`display_border.html`** — Border rendering (solid, dashed, colors)
- [x] `index.html` Body white background
- [x] `index.html` `<th>` grey background (#f0f0f0)
- [x] `index.html` `.box` border (#aaa)
- [x] `index.html` Table cell borders (#ccc)
- [x] `index.html` `overflow-hidden` yellow background (#ffd)
- [x] `index.html` `.flex-container` grey background (#f5f5f5)

### Text Rendering
- [x] **`display_draw_text.html`** — Text drawing with font, size, color
- [x] `index.html` Serif font-family on body
- [x] `index.html` Monospace font on `<code>` elements
- [x] `index.html` H1-H6 font-size rendering
- [x] `index.html` `<mark>` highlighted background
- [x] `index.html` `<strong>` bold text
- [x] `index.html` `<em>` italic text
- [x] **`html_text_formatting.html`** — `<u>` underline rendering
- [x] **`html_text_formatting.html`** — `<s>` strikethrough rendering

### Color
- [x] **`display_opacity.html`** — Opacity/transparency rendering
- [x] `index.html` `a { color: #00e }` link color
- [x] `index.html` `a:visited { color: #551a8b }` visited link color
- [x] `index.html` `.opacity-50` 50% opacity on text

### Stacking Order
- [x] **`display_stacking_order.html`** — Z-index and stacking context paint order

### Pseudo-element Generated Content
- [x] `index.html` `::before` with `content: "→ "` arrow
- [x] `index.html` `::after` with `content: " ←"` arrow
- [x] **`css_pseudo_first_line_letter.html`** — `::first-letter` drop cap with float
- [x] **`css_pseudo_first_line_letter.html`** — `::first-line` small-caps variant

---

## Summary

| Phase | Stage | Individual Tests | Integration (index.html) |
|-------|-------|-----------------|--------------------------|
| 1 | HTML DOM | 24/30 passing | PASS |
| 2 | CSS Parse | All passing | PASS |
| 3 | Cascade | All passing | PASS |
| 4 | Layout | All passing | PASS |
| 5 | Display List | All passing | PASS |

### Remaining Failures (6 Phase 1 HTML tests)
- `html_adoption_agency.html` — Adoption agency algorithm
- `html_cdata_svg.html` — CDATA in SVG
- `html_foster_parenting.html` — Foster parenting
- `html_malformed.html` — Malformed HTML
- `html_noscript.html` — Noscript element
- `html_template_element.html` — Template element

## How to Use

1. **Pick one line** from the roadmap
2. **Run the specific test**: `.\tools\run_tests.ps1 -filter "<testfile>"`
3. **Find the diff**: Check `tools/results/latest_run.txt` for the failure
4. **Fix the feature** in the engine code
5. **Rebuild**: `ninja -C build browser`
6. **Re-run** the same filter to verify the fix
7. **Mark `[x]`** on the roadmap
8. **Move to the next feature**

> Run phases in order: Phase 1 (DOM) → Phase 2 (CSS) → Phase 3 (Cascade) → Phase 4 (Layout) → Phase 5 (Display).
> A DOM parsing failure will cause cascading failures in later phases — fix DOM first.

### Quick Filter Reference
```powershell
.\tools\run_tests.ps1 -filter "index"                    # Comprehensive integration test
.\tools\run_tests.ps1 -filter "details_summary"          # details/summary elements
.\tools\run_tests.ps1 -filter "datalist_progress_meter"  # datalist/progress/meter
.\tools\run_tests.ps1 -filter "definition_list"          # dl/dt/dd
.\tools\run_tests.ps1 -filter "semantic_inline"          # abbr/kbd/var/samp/cite/time
.\tools\run_tests.ps1 -filter "text_formatting"          # u/s/sub/sup/mark
.\tools\run_tests.ps1 -filter "semantic_structural"      # article/header/footer/aside/section/nav/main/figure
.\tools\run_tests.ps1 -filter "tabindex_aria"            # tabindex/role/aria-label/lang/dir
.\tools\run_tests.ps1 -filter "visibility_clamp"         # visibility/clamp()
.\tools\run_tests.ps1 -filter "transition_transform"     # transition/transform
.\tools\run_tests.ps1 -filter "pseudo_first_line_letter" # ::first-line/::first-letter
.\tools\run_tests.ps1 -filter "media_print"              # @media print
```
