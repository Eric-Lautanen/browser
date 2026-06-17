# AGENTS.md — Browser Engine Agent Guide

## Project Identity

From-scratch C++20 web browser for Windows x86-64. Zero third-party dependencies. Every component — HTML5 parser, CSS layout (block/flex/grid), JavaScript VM+JIT, TLS 1.3, HTTP/2, font rasterizer, OpenGL renderer — is hand-written. ~25K LOC. Read `structure.md` for the complete file tree and CMake targets before touching any source.

---

## Core Principles (Non-Negotiable)

**Zero dependencies.** No Boost, Qt, WebKit, libcurl, OpenSSL, zlib, FreeType, WIC, COM, or any third-party library. Only: C++ stdlib, Win32 API (kernel32, user32, gdi32, ws2_32, bcrypt, opengl32), OpenGL.

**No exceptions.** The project compiles with `-fno-exceptions`. Use `Result<T, E>` for error propagation. Never write `throw`, `try`, or `catch`. Never use `std::exception_ptr`.

**No mutable globals.** All cross-thread data via `channel<T>`. Shared state uses `async::mutex`. Every parser/engine function takes all state as parameters or struct members — no static mutable variables.

**Modern C++20 only.** RAII, move semantics, `std::unique_ptr`/`shared_ptr`, `std::optional`, `std::variant`, `std::string_view`, `constexpr`, coroutines. No raw `new`/`delete`.

**Build must be clean.** `-Wall -Wextra -Wpedantic -Werror` enforced. Zero warnings on all changed files, always.

**Standards-compliant.** HTML5 (WHATWG living standard), CSS (W3C), ECMAScript (latest). Full compliance, not "good enough."

**No telemetry, ever.** `telemetry.cpp` generates a local `about:performance` report only. Never sends data anywhere. This is non-negotiable.

**Web research first.** If an implementation step is unclear, search for current (2026-era) solutions before writing code — C++20/23 standards, Win32 API patterns, protocol specs. Do not guess or rely on pre-2025 knowledge.

---

## Code Style — clang-format and clang-tidy

The repo ships a `.clang-format` at the root. Every changed `.cpp` and `.hpp` file must be format-clean before committing. Run format checks like this:

```bash
# Check only staged/changed files (what CI does):
git clang-format --diff HEAD

# Auto-fix in place:
git clang-format HEAD

# Check a specific file:
clang-format --dry-run --Werror path/to/file.cpp
```

clang-tidy must also produce zero warnings on changed files:

```bash
# Generate compile_commands.json first (required by clang-tidy):
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B build

# Run on changed files:
clang-tidy -p build path/to/changed_file.cpp
```

**Never commit code that fails either check.** If clang-tidy flags something you intentionally want to suppress, use a `// NOLINT(check-name)` comment on that line with a brief explanation — do not disable checks project-wide.

---

## C++ Unit Tests — What a Real Test Looks Like

Tests live in `tests/` and use the project's own framework (`tests/test_framework.hpp`): `TEST(SuiteName, CaseName) { ... }` with `ASSERT_EQ`, `ASSERT_TRUE`, `ASSERT_FALSE`. Each subsystem has its own `*_test.cpp` and its own test executable target in `CMakeLists.txt`.

**A test must actually test something.** The following patterns are forbidden — they compile, they pass, and they prove nothing:

```cpp
// WRONG — no assertion, just calls the function
TEST(Parser, ParsesDiv) {
    auto doc = html::parse("<div>hello</div>");
}

// WRONG — asserts the obvious without exercising the real behavior
TEST(Layout, FlexGrow) {
    ASSERT_TRUE(true);
}

// WRONG — stub that was never filled in
TEST(Cascade, ImportantOverride) {
    // TODO
}
```

Every test must have at least one `ASSERT_*` that would **actually fail** if the code under test is broken. Ask yourself: "If I delete the function this test is calling and replace it with `return {};`, does this test catch that?" If the answer is no, the test is useless.

**What good tests look like:**

```cpp
// Tests the actual computed value
TEST(Cascade, ImportantOverrideBeatsClassSelector) {
    auto doc = html::parse("<p class='special'>text</p>");
    // p { color: red !important; }  .special { color: blue; }
    auto styles = css::cascade(doc, sheet);
    auto* p = doc->querySelector("p");
    ASSERT_EQ(styles[p].color, Color::red());  // !important must win
}

// Tests a known bad input produces the correct error
TEST(Layout, AbsoluteChildDoesNotInflateParent) {
    // Render a parent with one absolute child taller than the parent.
    // Parent height must equal the in-flow content, not the absolute child.
    auto layout = run_layout("<div id='p' style='width:100px'>"
                             "  <div style='height:20px'></div>"
                             "  <div style='position:absolute;height:500px'></div>"
                             "</div>");
    auto* parent = layout->find_by_id("p");
    ASSERT_EQ(parent->content.height, 20.0f);  // not 500
}

// Tests a boundary condition explicitly
TEST(Flexbox, GrowRatiosAreProportional) {
    // grow:1, grow:2, grow:3 — total 6 units over 600px free space
    // Expected widths: 100px, 200px, 300px
    auto layout = run_flex_layout(600, {1, 2, 3});
    ASSERT_EQ(layout.children[0].content.width, 100.0f);
    ASSERT_EQ(layout.children[1].content.width, 200.0f);
    ASSERT_EQ(layout.children[2].content.width, 300.0f);
}
```

**Rules:**
- Every `TEST` block must contain at least one `ASSERT_*` that exercises a real invariant.
- Test the output, not just that the call didn't crash. Crashes are caught by the process exit code; wrong values are not.
- Cover the failure case, not just the happy path. If the bug was "X returned Y instead of Z", the test should assert Z.
- No `TODO` tests. A placeholder test with no assertions is worse than no test — it creates false confidence.
- Name the test after what it verifies, not what function it calls: `FlexGrowDistributesProportionally` not `TestFlexGrow`.

---

## Test Harness — Read This Before Every Task

All testing is in `tools/`. **Read `tools/README.md` in full before running or modifying any test tooling.** It defines every JSON schema, dump flag, diff mode, and workflow.

### How It Works

```
test.html ──► node tools/reference_html.js   ──► *.expected-dom.json ──┐
          └──► engine --dump-dom              ──► *.actual-dom.json   ──┴──► json_diff.py → PASS/FAIL/MINOR
```

Five pipeline stages, each independently testable:

| Stage | Engine flag | Reference | Diff mode |
|-------|------------|-----------|-----------|
| HTML parse → DOM | `--dump-dom` | `reference_html.js` (jsdom) | `--mode dom` |
| CSS parse → AST | `--dump-css` | `reference_css.js` (postcss) | `--mode css` |
| Cascade → computed styles | `--dump-cascade` | engine-bootstrapped fixture | `--mode cascade` |
| Layout → box tree | `--dump-layout` | engine-bootstrapped fixture | `--mode layout` |
| Paint → display list | `--dump-display-list` | engine-bootstrapped fixture | `--mode display-list` |

### Running Tests

```bash
# Full suite (human-readable table)
tools/run_tests.sh

# Full suite with machine-readable JSON output
tools/run_tests.sh --json
# → writes tools/results/latest_run.json

# Custom binary
BROWSER=build/browser.exe tools/run_tests.sh --json

# Single diff (any mode)
python3 tools/json_diff.py expected.json actual.json --mode dom
python3 tools/json_diff.py expected.json actual.json --mode layout --json-out entries.json
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All checks pass |
| 1 | One or more CRITICAL failures |
| 2 | MINOR failures only (no critical) |

### Output Files

- `tools/results/latest_run.txt` — full diff text of all failures
- `tools/results/latest_run.json` — structured JSON (with `--json` flag); machine-parse this
- `tools/results/history.txt` — per-test-per-run history; `grep <filename> history.txt` to see flakiness
- `tools/results/ref_errors.log` — Node.js reference script errors; check this if DOM/CSS tests show `REF_ERR`

### latest_run.json Schema

```json
{
  "timestamp": "2026-06-17T14:30:22",
  "total": 74, "passed": 68, "failed": 6, "critical": 4,
  "tests": [{
    "file": "layout_flexbox.html",
    "stages": { "dom": "PASS", "css": "-", "cascade": "SKIP", "layout": "FAIL", "display_list": "-" },
    "overall": "FAIL",
    "root_cause_stage": "layout",
    "secondary_failures": [],
    "timing_ms": { "dom_reference": 45, "dom_engine": 12, "layout_engine": 23 }
  }]
}
```

Use `root_cause_stage` to identify the actual bug. Failures in downstream stages (`cascade`, `layout`, `display_list`) when `dom` has also failed are secondary — fix the root cause first.

### Diff Entry Format

```
[CRITICAL] [LAYOUT_ERROR] root.children[0].content.width
  expected: 400.0
  actual:   784.0
```

Severities: `CRITICAL` = real bug (exit 1). `MINOR` = sub-pixel tolerance, whitespace (exit 2).

Categories: `PARSE_ERROR`, `CASCADE_ERROR`, `LAYOUT_ERROR`, `ENCODING_ERROR`, `MISSING_NODE`, `EXTRA_NODE`, `WRONG_VALUE`.

### Fixture Files

- `*.expected-dom.json` / `*.expected-css.json` — auto-generated by reference scripts; do not hand-edit
- `*.expected-cascade.json` / `*.expected-layout.json` / `*.expected-display-list.json` — engine-bootstrapped; regenerate from a known-good engine state when the engine's correct output changes
- `*.actual-*.json` — generated at test time; never commit these

### Adding a Regression Test

1. Create `tools/tests/regression_NNN_description.html` with the minimal HTML that reproduces the bug
2. DOM fixture is auto-generated on first run (jsdom reference)
3. After fixing the bug, bootstrap layout/cascade/display-list fixtures from the corrected engine:
   ```bash
   BROWSER=build/browser.exe
   $BROWSER --dump-layout tools/tests/regression_NNN_description.html > \
     tools/tests/regression_NNN_description.expected-layout.json
   ```
4. Run `tools/run_tests.sh --json` and verify the new test passes

---

## Build

```bash
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -S . -B build
ninja -C build browser
ninja -C build run_tests   # C++ unit test suite (30+ executables in tests/)
```

The C++ unit tests in `tests/` (custom framework: `TEST()` / `ASSERT_EQ()`) are separate from the external harness in `tools/`. Both must pass. The C++ tests cover individual subsystems (parser, VM, layout engine internals). The external harness tests end-to-end pipeline output against reference parsers.

---

## Directory Map (Quick Reference)

```
src/main.cpp          Entry point, BrowserWindow
html/                 HTML5 tokenizer, parser, DOM
css/                  CSS tokenizer, parser, cascade, layout, animation
  layout/             block, inline, flex, grid, table, positioning
  parser/             selector, declaration, at_rule
  cascade/            engine, specificity, inheritance, media
js/                   Lexer, parser, bytecode compiler, VM, GC, JIT
net/                  DNS, HTTP/1.1, HTTP/2, TLS 1.3, WebSocket, cache
render/               OpenGL renderer, fonts, paint display list, compositor
  paint/              commands, painter, executor
  font/               TrueType, atlas, rasterizer
browser/              Chrome UI, page loader, history, bookmarks, settings
async/                C++20 coroutines, channels, thread pool, IOCP
image/                BMP, PNG, GIF, JPEG decoders (all hand-written)
platform/             Win32 window, WGL context, audio
tests/                C++ unit tests (30+ executables)
tools/                External test harness (this section)
  run_tests.sh        Test runner
  json_diff.py        Diff engine
  reference_html.js   jsdom reference (DOM stage)
  reference_css.js    postcss reference (CSS stage)
  tests/              HTML/CSS input files + expected fixtures
  results/            Run output (latest_run.txt, latest_run.json, history.txt)
```

---

## Workflow Checklist (Every Task)

1. **Read `structure.md`** — understand which files are involved before writing any code
2. **Read `tools/README.md`** — if the task touches tests or the harness
3. **Run baseline** — `tools/run_tests.sh --json` before making changes; capture the baseline JSON
4. **Make changes** — follow all Core Principles; no new warnings
5. **Build** — `ninja -C build browser` must succeed with zero warnings
6. **Run C++ tests** — `ninja -C build run_tests`; all passing tests must still pass
7. **Run harness** — `tools/run_tests.sh --json`; compare `latest_run.json` to baseline
8. **Check `ref_errors.log`** — must be empty
9. **Format** — `git clang-format` on changed files
10. **Commit** — descriptive message; reference the failing test or bug being fixed

**Never** break a passing test. If a test was passing before your change and fails after, stop and fix it before proceeding.

---

## Common Pitfalls

- `task<Result<T>>` is wrong — double-wraps. Use `task<T>` (its `await_resume()` already returns `Result<T>`).
- `task<std::string>` is impossible — `Result<T,E=std::string>` asserts `T != E`. Use `task<std::vector<u8>>`.
- `task<void>` cannot return errors — use `task<bool>` for fallible void operations.
- `.sync_wait()` from a thread pool thread deadlocks — always `co_await` instead.
- `ConnectEx` requires `bind()` first.
- DOM native functions: `args[0]` is `this`; first JS argument is `args[1]`.
- `GCJSFunction::mark_children` must trace `prototype_property` or the prototype is GC'd.
- Timer callbacks that iterate `timer_queue` while potentially modifying it — collect IDs first, fire in a separate loop.
- Canvas textures cannot be cached by pixel-buffer address — regenerate each frame.