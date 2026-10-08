<#
    Assembles the portable Windows release from an already deployed build.

    Prerequisites: run scripts\build-windows.ps1 -Deploy first, so that
    build\bin contains the executables plus the Qt runtime.

    Usage:
        .\tools\package-release.ps1
        .\tools\package-release.ps1 -Flavor with-plugins      # 追加示例插件包，名字带后缀
        .\tools\package-release.ps1 -Version 1.1.0 -DistDir dist

    Two flavours exist and they deliberately get **different** folder names so the
    "plain" build never overwrites the one that ships the example plugin packages:

        McServerManager-<版本>-win64                纯程序
        McServerManager-<版本>-win64-with-plugins   附加 plugin-packages\*.zip
#>
param(
    [string]$BinDir = "build\bin",
    [string]$DistDir = "dist",
    [string]$Version = "1.1.0",
    [ValidateSet("plain", "with-plugins")]
    [string]$Flavor = "plain"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $BinDir "McServerManager.exe"))) {
    throw "missing $BinDir\McServerManager.exe - build with -Deploy first"
}
if (-not (Test-Path (Join-Path $BinDir "mcsm-cli.exe"))) {
    throw "missing $BinDir\mcsm-cli.exe - build the backend first"
}
if (-not (Test-Path (Join-Path $BinDir "Qt6Core.dll"))) {
    Write-Warning "Qt6Core.dll not found next to the exe: run build-windows.ps1 -Deploy for a self-contained package"
}

$suffix = if ($Flavor -eq "with-plugins") { "-with-plugins" } else { "" }
$name = "McServerManager-$Version-win64$suffix"
$stage = Join-Path $DistDir $name
if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Write-Host "copying runtime..." -ForegroundColor Cyan
Copy-Item (Join-Path $BinDir "*") -Destination $stage -Recurse -Force

foreach ($extra in @("README.md", "packaging\QUICKSTART.txt")) {
    if (Test-Path $extra) {
        Copy-Item $extra -Destination $stage -Force
    }
}

# docs (plugin SDK + API reference) travel with the release so the "接口文档"
# button inside the app works on a portable copy
if (Test-Path "docs") {
    Copy-Item "docs" -Destination $stage -Recurse -Force
}
if (Test-Path "examples\plugins") {
    New-Item -ItemType Directory -Force -Path (Join-Path $stage "examples") | Out-Null
    Copy-Item "examples\plugins" -Destination (Join-Path $stage "examples") -Recurse -Force
}

# bundled 7-Zip: the backend unpacks plugin packages with it, so the target
# machine never needs 7-Zip installed. Archive::toolPath() looks for
# <exeDir>/7zip/7za.exe first.
$sevenZip = "third_party\7zip\7za.exe"
if (-not (Test-Path $sevenZip)) {
    throw "missing $sevenZip - the plugin package manager needs the bundled 7-Zip"
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage "7zip") | Out-Null
Copy-Item $sevenZip -Destination (Join-Path $stage "7zip") -Force
if (Test-Path "third_party\7zip\LICENSE-7zip.txt") {
    Copy-Item "third_party\7zip\LICENSE-7zip.txt" -Destination (Join-Path $stage "7zip") -Force
}

# "with-plugins" flavour: ship the ready to import example plugin packages so a
# tester can load them from 设置 → 插件扩展 → 添加插件 (they are NOT pre-installed;
# the app data directory is %LOCALAPPDATA%\McServerManager).
if ($Flavor -eq "with-plugins") {
    $pluginDir = Join-Path $DistDir "plugins"
    if (-not (Get-ChildItem $pluginDir -Filter *.zip -ErrorAction SilentlyContinue)) {
        Write-Host "no packaged plugins found - running examples\plugins\build-examples.ps1" -ForegroundColor Yellow
        & (Join-Path $PSScriptRoot "..\examples\plugins\build-examples.ps1") | Out-Null
    }
    $target = Join-Path $stage "plugin-packages"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item (Join-Path $pluginDir "*.zip") -Destination $target -Force
    $packages = (Get-ChildItem $target -Filter *.zip).Count
    Write-Host ("bundled {0} example plugin package(s)" -f $packages) -ForegroundColor Cyan
}

$total = (Get-ChildItem $stage -Recurse -File | Measure-Object -Property Length -Sum).Sum
$fileCount = (Get-ChildItem $stage -Recurse -File | Measure-Object).Count

$zipPath = Join-Path $DistDir "$name.zip"
if (Test-Path $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Write-Host "compressing..." -ForegroundColor Cyan
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zipPath -CompressionLevel Optimal

$zipSize = (Get-Item $zipPath).Length
Write-Host ""
Write-Host "release ready:" -ForegroundColor Green
Write-Host ("  folder : {0}  ({1} files, {2:N1} MB)" -f $stage, $fileCount, ($total / 1MB))
Write-Host ("  zip    : {0}  ({1:N1} MB)" -f $zipPath, ($zipSize / 1MB))
Write-Host ""
Write-Host "contents:" -ForegroundColor DarkGray
Get-ChildItem $stage | Sort-Object Name | ForEach-Object {
    $type = if ($_.PSIsContainer) { "dir " } else { "file" }
    Write-Host ("  [{0}] {1}" -f $type, $_.Name) -ForegroundColor DarkGray
}
