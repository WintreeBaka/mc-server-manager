<#
    Builds the whole project (backend CLI + desktop app) on Windows.

    The script discovers the Qt kit, its MinGW/bin directory, Ninja and CMake
    automatically, because g++ needs its own DLL directory on PATH (cc1plus.exe
    fails with 0xC0000135 otherwise).

    Examples:
        .\scripts\build-windows.ps1
        .\scripts\build-windows.ps1 -QtDir "D:\qt\6.11.2\mingw_64"
        .\scripts\build-windows.ps1 -QtDir "C:\Qt\6.6.3\msvc2019_64" -Deploy
#>
param(
    [string]$QtDir = "",
    [string]$BuildDir = "build",
    [string]$Generator = "",
    [string]$Config = "Release",
    [switch]$Deploy,
    [switch]$Reconfigure
)

$ErrorActionPreference = "Stop"

function Find-QtKit {
    $roots = @("C:\Qt", "D:\Qt", "D:\qt", (Join-Path $env:USERPROFILE "Qt"))
    $candidates = @()
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $candidates += Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+' } |
            ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
            Where-Object { $_.Name -match '^(mingw|msvc)' }
    }
    if ($candidates.Count -eq 0) { return "" }
    # newest Qt version first (guard against kits whose folder is not a version)
    $ranked = try {
        $candidates | Sort-Object { [version]($_.Parent.Name -replace '[^0-9.]', '') } -Descending
    } catch {
        $candidates
    }
    return ($ranked | Select-Object -First 1).FullName
}

if (-not $QtDir) {
    $QtDir = Find-QtKit
    if (-not $QtDir) { throw "no Qt kit found; pass -QtDir <path to a Qt kit>" }
    Write-Host "auto-detected Qt kit: $QtDir" -ForegroundColor DarkGray
}
if (-not (Test-Path $QtDir)) { throw "Qt kit not found: $QtDir" }
$QtDir = (Resolve-Path $QtDir).Path

$qtRoot = Split-Path (Split-Path $QtDir -Parent) -Parent   # <root>\Tools\... lives here
$isMinGwKit = (Split-Path $QtDir -Leaf) -match '^mingw'
$compilerArgs = @()

if ($isMinGwKit) {
    $mingw = Get-ChildItem (Join-Path $qtRoot "Tools") -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^mingw' } |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($mingw) {
        $mingwBin = Join-Path $mingw.FullName "bin"
        if (Test-Path (Join-Path $mingwBin "g++.exe")) {
            # g++ needs its DLLs on PATH for cc1plus.exe; CMake needs absolute compilers
            $env:PATH = "$mingwBin;$env:PATH"
            $compilerArgs = @(
                "-DCMAKE_CXX_COMPILER=$(Join-Path $mingwBin 'g++.exe')",
                "-DCMAKE_C_COMPILER=$(Join-Path $mingwBin 'gcc.exe')"
            )
            Write-Host "MinGW       : $mingwBin" -ForegroundColor DarkGray
        }
    } else {
        Write-Warning "MinGW toolchain not found under $qtRoot\Tools - build may fail"
    }
} elseif ($QtDir -match 'msvc') {
    $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $cl) {
        Write-Warning "cl.exe not on PATH: start this script from a 'x64 Native Tools Command Prompt for VS'"
    }
}

$ninja = Get-ChildItem (Join-Path $qtRoot "Tools") -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq "Ninja" } | Select-Object -First 1
if ($ninja) { $env:PATH = (Join-Path $ninja.FullName "") + ";$env:PATH" }

$qtCmake = Get-ChildItem (Join-Path $qtRoot "Tools") -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "CMake*" } | Select-Object -First 1
if ($qtCmake) {
    $cmakeBin = Join-Path $qtCmake.FullName "bin"
    if (Test-Path (Join-Path $cmakeBin "cmake.exe")) { $env:PATH = "$cmakeBin;$env:PATH" }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found (install it, or use the CMake that ships with Qt Creator)"
}

if (-not $Generator) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) { $Generator = "Ninja" }
    elseif ($isMinGwKit) { $Generator = "MinGW Makefiles" }
}

if ($Reconfigure -and (Test-Path $BuildDir)) {
    Write-Host "removing previous build directory" -ForegroundColor DarkGray
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$configureArgs = @("-S", ".", "-B", $BuildDir, "-DCMAKE_PREFIX_PATH=$QtDir", "-DCMAKE_BUILD_TYPE=$Config")
if ($Generator) { $configureArgs += @("-G", $Generator) }
$configureArgs += $compilerArgs

Write-Host ""
Write-Host "==> configuring ($(if ($Generator) { $Generator } else { 'default generator' }), $Config)" -ForegroundColor Cyan
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "==> building" -ForegroundColor Cyan
& cmake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

if ($Deploy) {
    $windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
    if (Test-Path $windeployqt) {
        Write-Host "==> deploying Qt runtime" -ForegroundColor Cyan
        & $windeployqt "--release" "--no-translations" (Join-Path $BuildDir "bin\McServerManager.exe")
    } else {
        Write-Warning "windeployqt not found in $QtDir\bin"
    }
}

Write-Host ""
Write-Host "done:" -ForegroundColor Green
Write-Host "  backend : $BuildDir\bin\mcsm-cli.exe"
Write-Host "  desktop : $BuildDir\bin\McServerManager.exe"
Write-Host ""
Write-Host "smoke tests:" -ForegroundColor DarkGray
Write-Host "  powershell -File tools\smoke-test.ps1" -ForegroundColor DarkGray
Write-Host "  powershell -File tools\gui-smoke.ps1 -Screenshot .\smoke-shot.png" -ForegroundColor DarkGray
exit 0
