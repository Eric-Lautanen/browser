param(
    [switch]$quick      = $false,
    [switch]$full       = $false,
    [switch]$json       = $false,
    [string]$filter     = "",
    [string]$browser_bin = ""
)

$ErrorActionPreference = "Stop"
$SCRIPT_DIR   = Split-Path -Parent $MyInvocation.MyCommand.Path
$PROJECT_DIR  = Split-Path -Parent $SCRIPT_DIR
if (-not $browser_bin) { $browser_bin = Join-Path $PROJECT_DIR "build\browser.exe" }
$TESTS_DIR    = Join-Path $SCRIPT_DIR "tests"
$RESULTS_DIR  = Join-Path $SCRIPT_DIR "results"
$REF_HTML     = Join-Path $SCRIPT_DIR "reference_html.js"
$REF_CSS      = Join-Path $SCRIPT_DIR "reference_css.js"
$REF_ERRORS   = Join-Path $RESULTS_DIR "ref_errors.log"

New-Item -ItemType Directory -Path $RESULTS_DIR -Force | Out-Null

# Prerequisites
$missing = $false
if (-not (Get-Command node -ErrorAction SilentlyContinue))   { Write-Host "ERROR: node not found";    $missing = $true }
if (-not (Get-Command python3 -ErrorAction SilentlyContinue) -and
    -not (Get-Command python  -ErrorAction SilentlyContinue)) { Write-Host "ERROR: python not found"; $missing = $true }
if (-not (Test-Path $browser_bin)) { Write-Host "ERROR: browser not found at $browser_bin"; $missing = $true }
if ($missing) { exit 1 }

# Kill any leftover browser.exe from previous runs
Get-Process -Name "browser" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

# Determine filter
$filter_arg = ""
if ($full) {
    # no filter — run all
} elseif ($filter) {
    $filter_arg = $filter
} else {
    # quick mode — 14 representative tests
    $quick_tests = @(
        "html_basic_structure","html_attributes","html_entities","html_utf8",
        "css_selectors","css_values",
        "display_fill_rect","display_draw_text","display_background_color",
        "layout_block_model","layout_flexbox","layout_positioning",
        "cascade_important","cascade_specificity"
    )
    $filter_arg = ($quick_tests -join "|")
}

# Build argument list
$test_args = "--test-suite `"$TESTS_DIR`""
if ($filter_arg) { $test_args += " `"$filter_arg`"" }

Write-Host "Running browser test suite (single process)..."
Write-Host "Binary: $browser_bin"
if ($full) { Write-Host "Mode: full suite" }
elseif ($filter_arg) { Write-Host "Mode: quick ($((($filter_arg -split '\|').Length)) tests)" }
Write-Host ""

# Run ALL tests in ONE browser.exe process — no spawning, no accumulation
$psi = [System.Diagnostics.ProcessStartInfo]@{
    FileName               = $browser_bin
    Arguments              = $test_args
    RedirectStandardOutput = $false
    RedirectStandardError  = $true
    UseShellExecute        = $false
    CreateNoWindow         = $true
}
$p = [System.Diagnostics.Process]::Start($psi)
$stderr = $p.StandardError.ReadToEnd()
$p.WaitForExit(120000) | Out-Null
$ec = $p.ExitCode
$p.Dispose()

# Show results
if ($stderr) { Write-Host $stderr }

if ($ec -eq 0) { Write-Host "All tests passed."; exit 0 }
else { Write-Host "Some tests failed (exit code $ec)."; exit 1 }
