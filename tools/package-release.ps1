<#
    Assembles the portable Windows release from an already deployed build.

    Prerequisites: run scripts\build-windows.ps1 -Deploy first, so that
    build\bin contains the executables plus the Qt runtime.

    Usage:
        .\tools\package-release.ps1
        .\tools\package-release.ps1 -Version 1.0.0 -DistDir dist
#>
param(
    [string]$BinDir = "build\bin",
    [string]$DistDir = "dist",
    [string]$Version = "1.0.0"
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

$name = "McServerManager-$Version-win64"
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
