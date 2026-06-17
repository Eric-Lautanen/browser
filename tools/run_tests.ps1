param(
    [switch]$json = $false,
    [string]$browser_bin = ""
)

$ErrorActionPreference = "Stop"
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$PROJECT_DIR = Split-Path -Parent $SCRIPT_DIR
if (-not $browser_bin) { $browser_bin = Join-Path $PROJECT_DIR "build\browser.exe" }
$TESTS_DIR = Join-Path $SCRIPT_DIR "tests"
$RESULTS_DIR = Join-Path $SCRIPT_DIR "results"
$REF_HTML = Join-Path $SCRIPT_DIR "reference_html.js"
$REF_CSS = Join-Path $SCRIPT_DIR "reference_css.js"
$DIFF_ENGINE = Join-Path $SCRIPT_DIR "json_diff.py"
$LATEST_RUN = Join-Path $RESULTS_DIR "latest_run.txt"
$LATEST_JSON = Join-Path $RESULTS_DIR "latest_run.json"
$HISTORY_FILE = Join-Path $RESULTS_DIR "history.txt"
$REF_ERRORS = Join-Path $RESULTS_DIR "ref_errors.log"

New-Item -ItemType Directory -Path $RESULTS_DIR -Force | Out-Null
Remove-Item -Path $LATEST_RUN -Force -ErrorAction SilentlyContinue
Remove-Item -Path $REF_ERRORS -Force -ErrorAction SilentlyContinue

# Prerequisites check
$missing = $false
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { Write-Host "ERROR: node not found"; $missing = $true }
if (-not (Get-Command python3 -ErrorAction SilentlyContinue) -and -not (Get-Command python -ErrorAction SilentlyContinue)) { Write-Host "ERROR: python3 not found"; $missing = $true }
if (-not (Test-Path $browser_bin)) { Write-Host "ERROR: browser not found at $browser_bin"; $missing = $true }
if ($missing) { exit 1 }

$PYTHON = if (Get-Command python3 -ErrorAction SilentlyContinue) { "python3" } else { "python" }

# Install npm deps if needed
$node_modules = Join-Path $SCRIPT_DIR "node_modules"
if (-not (Test-Path $node_modules)) {
    Write-Host "Installing npm dependencies..."
    Push-Location $SCRIPT_DIR
    npm install 2>&1 | Out-Null
    Pop-Location
}

# Discover test files
$test_files = @()
Get-ChildItem -Path $TESTS_DIR -Filter "*.html" | Sort-Object Name | ForEach-Object { $test_files += $_.FullName }
Get-ChildItem -Path $TESTS_DIR -Filter "*.css" | Sort-Object Name | ForEach-Object { $test_files += $_.FullName }

Write-Host "Running browser test harness..."
Write-Host "Browser binary: $browser_bin"
Write-Host "Tests directory: $TESTS_DIR"
Write-Host "Found $($test_files.Count) test files"
Write-Host ""

# Table header
Write-Host ("{0,-42} {1,-6} {2,-5} {3,-8} {4,-6} {5,-4} {6}" -f "test", "DOM", "CSS", "CASCADE", "LAYOUT", "DISP", "RESULT")
Write-Host ("-" * 76)

$total = 0
$passed = 0
$failed = 0
$critical = 0

# Per-test results for history
$test_histories = @()

foreach ($tf in $test_files) {
    $basename = Split-Path -Leaf $tf
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($tf)
    $ext = [System.IO.Path]::GetExtension($tf).TrimStart('.')

    $dom_res = " - "
    $css_res = " - "
    $cascade_res = " - "
    $layout_res = " - "
    $disp_res = " - "
    $overall = "PASS"

    # Helper: run one stage
    function Run-Stage {
        param($stage, $dump_flag, $ref_script, $test_file, $stem, $TESTS_DIR, $REF_ERRORS, $browser_bin, $DIFF_ENGINE, $PYTHON, $LATEST_RUN)

        $expected = Join-Path $TESTS_DIR "$stem.expected-$stage.json"
        $actual = Join-Path $TESTS_DIR "$stem.actual-$stage.json"
        $result = "PASS"

        # Reference generation
        if ($ref_script -and (-not (Test-Path $expected))) {
            $output = & node $ref_script $test_file 2>>$REF_ERRORS
            if ($LASTEXITCODE -ne 0) { return @{ result = "REF_ERR" } }
            $output | Set-Content -Path $expected -NoNewline
        }

        if (-not (Test-Path $expected)) { return @{ result = "SKIP" } }

        # Engine dump (with timeout)
        $p = Start-Process -FilePath $browser_bin -ArgumentList "$dump_flag `"$test_file`"" -NoNewWindow -RedirectStandardOutput $actual -RedirectStandardError "nul" -PassThru
        $timedOut = -not $p.WaitForExit(10000)
        if ($timedOut) { $p.Kill(); return @{ result = "TIMEOUT" } }
        if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 124) { return @{ result = "ERR" } }
        if ($p.ExitCode -eq 124) { return @{ result = "TIMEOUT" } }

        # Diff
        $diff_out = & $PYTHON $DIFF_ENGINE $expected $actual --mode $stage 2>&1
        $diff_exit = $LASTEXITCODE
        $diff_out | Add-Content -Path $LATEST_RUN

        if ($diff_exit -eq 1) { $result = "FAIL" }
        elseif ($diff_exit -eq 2) { $result = "MINOR" }

        return @{ result = $result }
    }

    if ($ext -eq "html") {
        # DOM
        $r = Run-Stage "dom" "--dump-dom" $REF_HTML $tf $stem $TESTS_DIR $REF_ERRORS $browser_bin $DIFF_ENGINE $PYTHON $LATEST_RUN
        $dom_res = $r.result
        if ($dom_res -eq "FAIL" -or $dom_res -eq "REF_ERR" -or $dom_res -eq "ERR" -or $dom_res -eq "TIMEOUT") { $overall = "FAIL"; $critical++ }

        # Cascade
        $r = Run-Stage "cascade" "--dump-cascade" $null $tf $stem $TESTS_DIR $REF_ERRORS $browser_bin $DIFF_ENGINE $PYTHON $LATEST_RUN
        $cascade_res = $r.result
        if ($cascade_res -eq "FAIL" -or $cascade_res -eq "ERR" -or $cascade_res -eq "TIMEOUT") { $overall = "FAIL"; $critical++ }

        # Layout
        $r = Run-Stage "layout" "--dump-layout" $null $tf $stem $TESTS_DIR $REF_ERRORS $browser_bin $DIFF_ENGINE $PYTHON $LATEST_RUN
        $layout_res = $r.result
        if ($layout_res -eq "FAIL" -or $layout_res -eq "ERR" -or $layout_res -eq "TIMEOUT") { $overall = "FAIL"; $critical++ }

        # Display list
        $r = Run-Stage "display-list" "--dump-display-list" $null $tf $stem $TESTS_DIR $REF_ERRORS $browser_bin $DIFF_ENGINE $PYTHON $LATEST_RUN
        $disp_res = $r.result
        if ($disp_res -eq "FAIL" -or $disp_res -eq "ERR" -or $disp_res -eq "TIMEOUT") { $overall = "FAIL"; $critical++ }
    }
    elseif ($ext -eq "css") {
        $r = Run-Stage "css" "--dump-css" $REF_CSS $tf $stem $TESTS_DIR $REF_ERRORS $browser_bin $DIFF_ENGINE $PYTHON $LATEST_RUN
        $css_res = $r.result
        if ($css_res -eq "FAIL" -or $css_res -eq "REF_ERR" -or $css_res -eq "ERR" -or $css_res -eq "TIMEOUT") { $overall = "FAIL"; $critical++ }
    }

    if ($overall -eq "FAIL") { $failed++ } else { $passed++ }
    $total++

    Write-Host ("{0,-42} {1,-6} {2,-5} {3,-8} {4,-6} {5,-4} {6}" -f $basename, $dom_res, $css_res, $cascade_res, $layout_res, $disp_res, $overall)

    $test_histories += @{ name = $basename; dom = $dom_res; css = $css_res; cascade = $cascade_res; layout = $layout_res; disp = $disp_res; overall = $overall }
}

Write-Host ""
Write-Host "Total: $total tests, $passed passed, $failed failed"
Write-Host "Critical failures: $critical"

# History
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
foreach ($th in $test_histories) {
    "$timestamp | $($th.name) | dom=$($th.dom) css=$($th.css) cascade=$($th.cascade) layout=$($th.layout) disp=$($th.disp) | $($th.overall)" | Add-Content -Path $HISTORY_FILE
}

# JSON output
if ($json) {
    $json_tests = $test_histories | ForEach-Object {
        @{
            file = $_.name
            stages = @{ dom = $_.dom; css = $_.css; cascade = $_.cascade; layout = $_.layout; display_list = $_.disp }
            overall = $_.overall
        }
    }
    $json_output = @{
        timestamp = (Get-Date -Format "o")
        browser_binary = $browser_bin
        total = $total
        passed = $passed
        failed = $failed
        critical = $critical
        tests = $json_tests
    }
    $json_output | ConvertTo-Json -Depth 10 | Set-Content -Path $LATEST_JSON
    Write-Host "JSON results written to: $LATEST_JSON"
}

if ($global:failed -gt 0) {
    Write-Host "Full diff output written to: $LATEST_RUN"
    if ($json) { Write-Host "JSON output written to: $LATEST_JSON" }
    exit 1
}
