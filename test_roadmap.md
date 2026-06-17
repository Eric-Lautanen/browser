# Test Roadmap — Root `index.html` Coverage

> One testable feature per line. Run each with `.\tools\run_tests.ps1 -filter "<name>"`.
> Fix features in order — downstream stages depend on upstream ones.

---

## Phase 1: HTML Parsing (DOM Stage)

### Core Structure
- [ ] **`index`** — Full comprehensive test page: DOCTYPE, html, head, body, meta charset, viewport, title, link favicon
- [ ] **`html_basic_structure.html`** — Fundamental document with html/head/body

### Headings (`html_basic_structure.html` covers this, but integration test covers all h1-h6)
- [ ] `index.html` `<h1>` through `<h6>` all six heading levels

### Text & Inline Elements
- [ ] `index.html` `<strong>`, `<em>`, `<code>`, `<a>` inline elements
- [ ] **`html_text_formatting.html`** — `<u>`, `<s>`, `<sub>`, `<sup>`, `<mark>` formatting
- [ ] **`html_semantic_inline.html`** — `<abbr>`, `<kbd>`, `<var>`, `<samp>`, `<cite>`, `<time>`
- [ ] `index.html` HTML entities: `&amp;`, `&lt;`, `&gt;`, `&quot;`, `&copy;`, `&mdash;`, `&nbsp;`
- [ ] `index.html` Non-breaking space `&nbsp;` rendered inline

### Media
- [ ] `index.html` `<img>` with src, alt, width, height attributes
- [ ] **`html_semantic_structural.html`** — `<figure>` / `<figcaption>` container
- [ ] `index.html` `<picture>` / `<source>` with `srcset` / `media` attributes
- [ ] `index.html` Inline `<svg>` with `<rect>`, `<circle>`, `<polygon>`, `<text>`
- [ ] `index.html` `<link>` with `data:` URI href

### Lists
- [ ] `index.html` Nested `<ul>` / `<li>` unordered lists
- [ ] `index.html` Nested `<ol>` / `<li>` ordered lists
- [ ] **`html_definition_list.html`** — `<dl>`, `<dt>`, `<dd>` definition lists

### Tables
- [ ] `index.html` `<table>` with `<thead>`, `<tbody>`, `<tfoot>`
- [ ] `index.html` `<th>` with `rowspan` attribute
- [ ] `index.html` `<th>` with `colspan` attribute
- [ ] `index.html` `<tfoot>` with `colspan`

### Forms
- [ ] `index.html` `<form>` with `action`, `method` attributes
- [ ] `index.html` `<input type="text">` with placeholder
- [ ] `index.html` `<input type="password">`
- [ ] `index.html` `<input type="number">` with min/max/value
- [ ] `index.html` `<textarea>` with rows/cols/default text
- [ ] `index.html` `<input type="checkbox">` with `checked` attribute
- [ ] `index.html` `<input type="radio">` with `checked` + name grouping
- [ ] `index.html` `<select>` / `<option>` with `selected` attribute
- [ ] `index.html` `<input type="email">` with placeholder
- [ ] `index.html` `<input type="search">` with placeholder
- [ ] `index.html` `<input type="date">` with value
- [ ] `index.html` `<input type="range">` with min/max/value
- [ ] `index.html` `<input type="color">` with value
- [ ] **`html_datalist_progress_meter.html`** — `<datalist>` with options
- [ ] **`html_datalist_progress_meter.html`** — `<progress>` with value/max
- [ ] **`html_datalist_progress_meter.html`** — `<meter>` with low/high/optimum
- [ ] `index.html` `<button type="submit">`, `<button type="reset">`
- [ ] `index.html` `<label>` with `for` attribute
- [ ] `index.html` `data-*` attributes (`data-testid`, `data-custom-attr`)

### Semantic / Structural
- [ ] **`html_semantic_structural.html`** — `<nav>`, `<main>`, `<article>`, `<section>`, `<aside>`
- [ ] `index.html` `<article>` with nested `<header>` and `<footer>`
- [ ] `index.html` `<section>` element
- [ ] `index.html` `<aside>` element
- [ ] **`html_semantic_structural.html`** — `<figure>` / `<figcaption>`
- [ ] **`html_details_summary.html`** — `<details>` / `<summary>` elements
- [ ] **`html_details_summary.html`** — `<details open>` attribute
- [ ] `index.html` `<blockquote>` element
- [ ] `index.html` `<figure>` / `<figcaption>` in integration context
- [ ] `index.html` `<pre>` / `<code>` block
- [ ] `index.html` `<hr>` horizontal rule

### Attributes
- [ ] `index.html` `id` attribute on headings (e.g., `id="headings"`)
- [ ] `index.html` `class` attribute on elements
- [ ] **`html_tabindex_aria.html`** — `tabindex="0"` and `tabindex="-1"`
- [ ] **`html_tabindex_aria.html`** — `role` attribute (ARIA)
- [ ] **`html_tabindex_aria.html`** — `aria-label` attribute
- [ ] **`html_tabindex_aria.html`** — `aria-hidden` attribute
- [ ] **`html_tabindex_aria.html`** — `lang` attribute (`lang="fr"`, `lang="ja"`, `lang="ar"`)
- [ ] **`html_tabindex_aria.html`** — `dir` attribute (`dir="rtl"`)
- [ ] `index.html` `lang="ja"` + `data-label="日本語"` on same element

### Parser Stress
- [ ] `index.html` Optional closing tags (`<li>` without `</li>`, `<p>` without `</p>`)
- [ ] `index.html` Mixed-case tag names (`<P>`, `<DIV>`, `<SPAN>`, `<CODE>`)
- [ ] `index.html` Deep nesting (5 levels of `<div>`)
- [ ] `index.html` Unicode content (Japanese, Arabic, emoji)
- [ ] `index.html` Comment edge cases (empty comment, double-dash via entity)
- [ ] `index.html` Void elements: `<br>`, `<hr>`, `<img>`, `<input>`, `<meta>`, `<link>`
- [ ] `index.html` Whitespace collapse (multiple spaces in text)

---

## Phase 2: CSS Parsing (CSS Stage)

### Selectors
- [ ] `index.html` Universal selector `*` with `::before`, `::after`
- [ ] `index.html` `:root` pseudo-class
- [ ] `index.html` Type selectors (`h1`, `p`, `a`, `code`, `pre`, `table`, etc.)
- [ ] `index.html` Class selectors (`.box`, `.flex-container`, `.grid-item`, etc.)
- [ ] `index.html` Descendant combinator (`pre code`, `ul li`, etc.)
- [ ] `index.html` `:first-child` pseudo-class
- [ ] `index.html` `:last-child` pseudo-class
- [ ] `index.html` `:nth-child(even)` pseudo-class
- [ ] `index.html` `:not(.excluded)` negation pseudo-class
- [ ] `index.html` `:hover` pseudo-class (interactive)
- [ ] `index.html` `:focus` pseudo-class (interactive)
- [ ] `index.html` `:visited` pseudo-class

### Pseudo-elements
- [ ] **`css_pseudo_first_line_letter.html`** — `::first-line` pseudo-element
- [ ] **`css_pseudo_first_line_letter.html`** — `::first-letter` pseudo-element (with float, font-size, color)
- [ ] `index.html` `::before` with `content` property
- [ ] `index.html` `::after` with `content` property

### At-Rules / Media Queries
- [ ] **`css_media_print.html`** — `@media print` with `display: none`
- [ ] `index.html` `@media (max-width: 600px)` with grid/flex changes
- [ ] `index.html` `@media (prefers-color-scheme: dark)` with `::after` content

### CSS Values & Functions
- [ ] `index.html` CSS custom properties (`--color-primary`, `--color-accent`, etc.)
- [ ] `index.html` `var()` function (`var(--color-primary)`, `var(--spacing-md)`)
- [ ] `index.html` `calc(100% - 4em)` function in width
- [ ] **`layout_visibility_clamp.html`** — `clamp(0.85rem, 2vw, 1.25rem)` in font-size
- [ ] `index.html` `rgba()` color function (`.z-on-top` background)
- [ ] `index.html` `url('data:...')` for favicon

---

## Phase 3: Cascade (Cascade Stage)

### Style Resolution
- [ ] `index.html` UA stylesheet: block display for body, h1-h6, p, etc.
- [ ] `index.html` Inline `style` attribute overrides `<style>` block
- [ ] `index.html` `var(--color-primary)` resolves to `#005fcc` in cascade
- [ ] `index.html` `var(--spacing-md)` resolves to `1em` in padding
- [ ] `index.html` `var(--radius)` resolves to `4px` in border-radius
- [ ] `index.html` `var(--color-accent)` resolves to `#e63946` in hover color
- [ ] **`css_pseudo_first_line_letter.html`** — `::first-line` `font-variant: small-caps`
- [ ] **`css_pseudo_first_line_letter.html`** — `::first-letter` `font-size: 2.5em`, `float: left`
- [ ] `index.html` `display: none` on `.display-none`
- [ ] `index.html` `display: inline-block` on `.display-inline-block`
- [ ] **`layout_visibility_clamp.html`** — `visibility: hidden` computed style
- [ ] **`layout_visibility_clamp.html`** — `visibility: visible` computed style

---

## Phase 4: Layout (Layout Stage)

### Box Model
- [ ] `index.html` Body margin: `2em auto`, max-width: `800px`
- [ ] `index.html` `box-sizing: border-box` on all elements
- [ ] `index.html` H1-H6 font-size cascade (2em, 1.5em, 1.2em)
- [ ] `index.html` `<hr>` height from border-top
- [ ] `index.html` Inline `<code>` background/padding

### Block Layout
- [ ] `index.html` Block stacking: h1, h2, p, ul, ol, table, form, etc.
- [ ] `index.html` `<blockquote>` border-left, padding-left, margin-left: 0

### Inline Layout
- [ ] `index.html` `<strong>` inline, `<em>` inline, `<code>` inline
- [ ] **`html_text_formatting.html`** — `<u>` inline, `<s>` inline, `<sub>` inline, `<sup>` inline, `<mark>` inline
- [ ] `index.html` `<abbr>`, `<kbd>`, `<var>`, `<samp>`, `<cite>` inline flow
- [ ] `index.html` `<img>` inline replaced element
- [ ] `index.html` `<br>` line break within `<p>`

### Lists
- [ ] `index.html` Nested `<ul>` indentation
- [ ] `index.html` Nested `<ol>` indentation
- [ ] **`html_definition_list.html`** — `<dl>` block, `<dt>` block, `<dd>` indented
- [ ] `index.html` `<li>` with nested `<ul>` as child

### Tables
- [ ] `index.html` Table: `<thead>`, `<tbody>`, `<tfoot>` display types
- [ ] `index.html` Table: `colspan` and `rowspan` cell spanning
- [ ] `index.html` `<th>` centered and bold
- [ ] `index.html` `<td>` left-aligned

### Forms
- [ ] `index.html` `<form>` as block with margin
- [ ] `index.html` `<input>` inline-block replaced elements
- [ ] `index.html` `<textarea>` inline-block with rows/cols sizing
- [ ] `index.html` `<select>` inline-block with options
- [ ] `index.html` `<button>` inline-block with text
- [ ] **`html_datalist_progress_meter.html`** — `<progress>` inline-block
- [ ] **`html_datalist_progress_meter.html`** — `<meter>` inline-block

### Positioning
- [ ] `index.html` `position: relative` on `.pos-container`
- [ ] `index.html` `position: absolute` on `.pos-absolute` (top, right)
- [ ] `index.html` `position: sticky` with `top: 0`
- [ ] `index.html` `z-index: 1` vs `z-index: 10` stacking

### Overflow
- [ ] `index.html` `overflow: hidden` clipping content
- [ ] `index.html` `overflow: scroll` creating scroll container
- [ ] `index.html` `overflow: auto` conditional scroll

### Flexbox
- [ ] `index.html` `display: flex` on `.flex-container`
- [ ] `index.html` `flex-wrap: wrap` wrapping children
- [ ] `index.html` `gap: 0.5em` spacing between flex items
- [ ] `index.html` `align-items: center` cross-axis alignment
- [ ] `index.html` `flex: 1 1 80px` shorthand on `.flex-item`
- [ ] `index.html` `flex-grow: 2` on `.flex-item.grow2`
- [ ] `index.html` `flex-direction: column` on `.flex-col`
- [ ] `index.html` `justify-content: space-between` alignment
- [ ] `index.html` `align-items: flex-end` cross-axis end alignment
- [ ] `index.html` `height: 60px`/`30px`/`50px`/`40px` flex item heights

### Grid
- [ ] `index.html` `display: grid` on `.grid-container`
- [ ] `index.html` `grid-template-columns: repeat(3, 1fr)` track sizing
- [ ] `index.html` `gap: 0.5em` grid gutters
- [ ] `index.html` `grid-column: span 2` item spanning
- [ ] `index.html` `grid-template-areas` named areas
- [ ] `index.html` `grid-area: header / sidebar / content / footer`

### Visibility / Opacity
- [ ] `index.html` `display: none` element takes no space
- [ ] **`layout_visibility_clamp.html`** — `visibility: hidden` preserves space
- [ ] `index.html` `opacity: 0.5` on `.opacity-50`
- [ ] **`layout_transition_transform.html`** — `transform: rotate(-2deg) translateX(8px)`
- [ ] **`layout_transition_transform.html`** — `transition` property (parsed/layout, animation not required)
- [ ] **`layout_visibility_clamp.html`** — `font-size: clamp(...)` resolves to valid length

### Calc
- [ ] `index.html` `width: calc(100% - 4em)` resolves to container width minus 4em

### Semantic Elements Layout
- [ ] **`html_semantic_structural.html`** — `<nav>` navigational block
- [ ] **`html_semantic_structural.html`** — `<main>` main content block
- [ ] **`html_semantic_structural.html`** — `<article>` self-contained content block
- [ ] **`html_semantic_structural.html`** — `<section>` thematic grouping block
- [ ] **`html_semantic_structural.html`** — `<aside>` sidebar block
- [ ] **`html_semantic_structural.html`** — `<header>`/introductory block
- [ ] **`html_semantic_structural.html`** — `<footer>` footer block
- [ ] `index.html` `<details>` / `<summary>` elements
- [ ] `index.html` Nested `<header>` inside `<article>`

### Inline SVG Layout
- [ ] `index.html` `<svg>` inline replaced element with width/height
- [ ] `index.html` SVG child elements: `<rect>`, `<circle>`, `<polygon>`, `<text>`

### Miscellaneous Layout
- [ ] `index.html` Long unbroken word (`supercalifragilisticexpialidocious`) wrapping
- [ ] `index.html` Whitespace collapse (multiple spaces → one)
- [ ] `index.html` `<small>` inline with smaller font
- [ ] `index.html` `<sup>` / `<sub>` vertical alignment
- [ ] `index.html` `tabindex="0"` span is still inline

---

## Phase 5: Display List / Paint (Display List Stage)

### Background & Border
- [ ] `index.html` Body white background
- [ ] `index.html` `<th>` grey background (#f0f0f0)
- [ ] `index.html` `.box` border (#aaa)
- [ ] `index.html` Table cell borders (#ccc)
- [ ] `index.html` `overflow-hidden` yellow background (#ffd)
- [ ] `index.html` `.flex-container` grey background (#f5f5f5)

### Text Rendering
- [ ] `index.html` Serif font-family on body
- [ ] `index.html` Monospace font on `<code>` elements
- [ ] `index.html` H1-H6 font-size rendering
- [ ] `index.html` `<mark>` highlighted background
- [ ] `index.html` `<strong>` bold text
- [ ] `index.html` `<em>` italic text
- [ ] **`html_text_formatting.html`** — `<u>` underline rendering
- [ ] **`html_text_formatting.html`** — `<s>` strikethrough rendering

### Color
- [ ] `index.html` `a { color: #00e }` link color
- [ ] `index.html` `a:visited { color: #551a8b }` visited link color
- [ ] `index.html` `.opacity-50` 50% opacity on text

### Pseudo-element Generated Content
- [ ] `index.html` `::before` with `content: "→ "` arrow
- [ ] `index.html` `::after` with `content: " ←"` arrow
- [ ] **`css_pseudo_first_line_letter.html`** — `::first-letter` drop cap with float
- [ ] **`css_pseudo_first_line_letter.html`** — `::first-line` small-caps variant

---

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
