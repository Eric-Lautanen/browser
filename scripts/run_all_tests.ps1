#!/usr/bin/env pwsh
$ErrorActionPreference = "Stop"
$rootDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $rootDir "build"

Write-Host "=== Build Debug ===" -ForegroundColor Cyan
Push-Location $rootDir
try {
    $configOut = cmake -G "Ninja" -B build -DCMAKE_BUILD_TYPE=Debug 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host $configOut -ForegroundColor Red
        throw "CMake configuration failed"
    }

    $buildOut = cmake --build build 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host $buildOut -ForegroundColor Red
        throw "CMake build failed"
    }
} finally {
    Pop-Location
}

Write-Host "`n=== Run All Tests ===" -ForegroundColor Cyan
$testExes = Get-ChildItem -Path $buildDir -Filter "*_test.exe" -Recurse | Where-Object {
    $_.Name -match "_test\.exe$" -and $_.Name -ne "test_framework_test.exe"
} | ForEach-Object { $_.FullName }

$failed = 0
$passed = 0
$errors = @()

foreach ($exe in $testExes) {
    $name = Split-Path -Leaf $exe
    Write-Host "  Running $name..." -NoNewline

    $output = & $exe 2>&1
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) {
        Write-Host " PASS" -ForegroundColor Green
        $passed++
    } else {
        Write-Host " FAIL (exit=$exitCode)" -ForegroundColor Red
        $failed++
        $summaryLine = $output | Select-String -Pattern '\d+/\d+ tests passed'
        if ($summaryLine) {
            $errors += "=== $name === $summaryLine"
        } else {
            $errors += "=== $name === (exit=$exitCode)"
        }
    }
}

Write-Host "`n=== Summary ===" -ForegroundColor Cyan
Write-Host "  Passed: $passed"
Write-Host "  Failed: $failed"
if ($errors.Count -gt 0) {
    Write-Host "`nFailure details:" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host $_ }
}
if ($failed -gt 0) { exit 1 }
Write-Host "All tests passed!" -ForegroundColor Green
exit 0
