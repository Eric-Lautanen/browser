#!/usr/bin/env pwsh
$ErrorActionPreference = "Stop"
$rootDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $rootDir "build"

Write-Host "=== Build Debug ===" -ForegroundColor Cyan
Push-Location $rootDir
try {
    cmake -G "Ninja" -B build -DCMAKE_BUILD_TYPE=Debug
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

    cmake --build build
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }
} finally {
    Pop-Location
}

Write-Host "`n=== Run All Tests ===" -ForegroundColor Cyan
$testExes = Get-ChildItem -Path $buildDir -Filter "*_test.exe" -Recurse | Where-Object {
    $_.Name -notmatch "\.lib$|\.pdb$|\.obj$|\.exp$" -and
    $_.Name -match "_test\.exe$"
} | ForEach-Object { $_.FullName }

$failed = 0
$passed = 0
$results = @()

foreach ($exe in $testExes) {
    $name = Split-Path -Leaf $exe
    Write-Host "  Running $name..." -NoNewline
    $proc = Start-Process -FilePath $exe -WorkingDirectory $rootDir -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $env:TEMP "test_output.txt") -RedirectStandardError (Join-Path $env:TEMP "test_error.txt")
    $proc.WaitForExit(120000)
    $exitCode = $proc.ExitCode
    $output = Get-Content (Join-Path $env:TEMP "test_output.txt") -Raw
    $errorOut = Get-Content (Join-Path $env:TEMP "test_error.txt") -Raw

    if ($exitCode -eq 0) {
        Write-Host " PASS" -ForegroundColor Green
        $passed++
    } else {
        Write-Host " FAIL (exit=$exitCode)" -ForegroundColor Red
        $failed++
        $results += "=== $name ==="
        if ($output) { $results += $output.Trim() }
        if ($errorOut) { $results += "STDERR: $($errorOut.Trim())" }
    }
}

Write-Host "`n=== Summary ===" -ForegroundColor Cyan
Write-Host "  Passed: $passed"
Write-Host "  Failed: $failed"
if ($results.Count -gt 0) {
    Write-Host "`nFailure details:" -ForegroundColor Red
    $results | ForEach-Object { Write-Host $_ }
    exit 1
}
Write-Host "All tests passed!" -ForegroundColor Green
exit 0
