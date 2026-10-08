<#
    把 examples\plugins 下的示例插件打包成可分发的 zip。

    用法：
        powershell -File examples\plugins\build-examples.ps1
        powershell -File examples\plugins\build-examples.ps1 -OutDir dist\plugins

    使用随管理器发布的内置 7-Zip（third_party\7zip\7za.exe），
    找不到时回退到系统 7z。
#>
param(
    [string]$OutDir = "dist\plugins"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $root "..\..")

$sevenZip = Join-Path $repo "third_party\7zip\7za.exe"
if (-not (Test-Path $sevenZip)) {
    $candidate = "C:\Program Files\7-Zip\7z.exe"
    if (Test-Path $candidate) { $sevenZip = $candidate } else { throw "找不到 7-Zip" }
}

$outPath = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force -Path $outPath | Out-Null

Get-ChildItem $root -Directory | Where-Object { Test-Path (Join-Path $_.FullName "plugin.json") } | ForEach-Object {
    $manifest = Get-Content (Join-Path $_.FullName "plugin.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    $name = "{0}-{1}.zip" -f $manifest.id, $manifest.version
    $target = Join-Path $outPath $name
    if (Test-Path $target) { Remove-Item -LiteralPath $target -Force }
    Push-Location $_.FullName
    try {
        & $sevenZip a -tzip $target "." -y -bso0 -bsp0 | Out-Null
    } finally {
        Pop-Location
    }
    Write-Host ("packed {0}  ->  {1}" -f $manifest.name, $target) -ForegroundColor Green
}
