# Build KeyShorts with the Visual Studio toolchain (falls back to a system cmake).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$cmake = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
        $candidate = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $candidate) { $cmake = $candidate }
    }
}
if (-not $cmake) { $cmake = "cmake" }

$buildDir = Join-Path $root "build"
& $cmake -S $root -B $buildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$exe = Join-Path $buildDir "Release\KeyShorts.exe"
Write-Host ""
Write-Host "Built: $exe" -ForegroundColor Green
