<#
    创建 GitHub Release 并上传附件（不依赖 gh CLI，只用 REST API）。

    需要一个对目标仓库有写权限的令牌（classic PAT 勾选 repo，或 fine-grained 勾选
    Contents: Read and write）。推荐放进环境变量，避免出现在命令历史里：

        $env:GITHUB_TOKEN = "ghp_xxx"
        .\tools\create-release.ps1

    默认行为（1.1.0）：
        tag     : v1.1.0
        说明    : docs\RELEASE_NOTES-1.1.0.md
        附件    : dist\McServerManager-1.1.0-win64.zip   （纯程序便携版）
                  docs\PLUGIN-SDK.md                     （接口文档，单独附件）

    已存在同名 Release 时会复用并补传缺失的附件，不会重复创建。
#>
param(
    [string]$Version = "1.1.0",
    [string]$Repo = "WintreeBaka/mc-server-manager",
    [string]$Tag = "",
    [string]$Title = "",
    [string]$NotesFile = "",
    [string[]]$Assets = @(),
    [string]$Token = $env:GITHUB_TOKEN,
    [switch]$Draft
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

if (-not $Tag) { $Tag = "v$Version" }
if (-not $Title) { $Title = "McServerManager $Version" }
if (-not $NotesFile) { $NotesFile = "docs\RELEASE_NOTES-$Version.md" }
if ($Assets.Count -eq 0) {
    $Assets = @("dist\McServerManager-$Version-win64.zip", "docs\PLUGIN-SDK.md")
}

if (-not $Token) {
    throw @"
缺少 GitHub 令牌。请先设置环境变量（不要写进脚本）：
    `$env:GITHUB_TOKEN = "ghp_xxx"      # 需要 repo / Contents: Read and write 权限
"@
}
if (-not (Test-Path $NotesFile)) { throw "找不到发行说明：$NotesFile" }
foreach ($asset in $Assets) {
    if (-not (Test-Path $asset)) { throw "找不到附件：$asset" }
}

$headers = @{
    Authorization = "Bearer $Token"
    Accept        = "application/vnd.github+json"
    'User-Agent'  = 'mcsm-release-script'
}
$api = "https://api.github.com/repos/$Repo"

Write-Host "release  : $Tag  ($Repo)" -ForegroundColor Cyan
Write-Host "notes    : $NotesFile"
foreach ($asset in $Assets) { Write-Host ("asset    : {0}  ({1:N1} MB)" -f $asset, ((Get-Item $asset).Length / 1MB)) }

$body = Get-Content $NotesFile -Raw -Encoding UTF8

# reuse an existing release for the tag instead of failing on 422
$release = $null
try {
    $release = Invoke-RestMethod -Uri "$api/releases/tags/$Tag" -Headers $headers -TimeoutSec 60
    Write-Host "已存在同名 Release，复用并补传附件" -ForegroundColor Yellow
} catch {
    $payload = @{
        tag_name   = $Tag
        name       = $Title
        body       = $body
        draft      = [bool]$Draft
        prerelease = $false
    } | ConvertTo-Json -Depth 4
    try {
        $release = Invoke-RestMethod -Uri "$api/releases" -Headers $headers -Method Post `
                                     -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($payload)) `
                                     -TimeoutSec 120
    } catch {
        $detail = $_.Exception.Message
        if ($_.ErrorDetails.Message) { $detail = $_.ErrorDetails.Message }
        throw "创建 Release 失败：$detail"
    }
    Write-Host "已创建 Release" -ForegroundColor Green
}

$existing = @()
try {
    $existing = (Invoke-RestMethod -Uri "$api/releases/$($release.id)/assets" -Headers $headers -TimeoutSec 60) |
                ForEach-Object { $_.name }
} catch { }

foreach ($asset in $Assets) {
    $name = Split-Path $asset -Leaf
    if ($existing -contains $name) {
        Write-Host ("附件已存在，跳过：{0}" -f $name) -ForegroundColor DarkGray
        continue
    }
    $uploadUrl = "https://uploads.github.com/repos/$Repo/releases/$($release.id)/assets?name=$([uri]::EscapeDataString($name))"
    try {
        $null = Invoke-RestMethod -Uri $uploadUrl -Headers $headers -Method Post `
                                  -ContentType 'application/octet-stream' -InFile $asset -TimeoutSec 1800
        Write-Host ("已上传：{0}" -f $name) -ForegroundColor Green
    } catch {
        $detail = $_.Exception.Message
        if ($_.ErrorDetails.Message) { $detail = $_.ErrorDetails.Message }
        Write-Host ("上传失败：{0} -> {1}" -f $name, $detail) -ForegroundColor Red
        throw "上传附件失败"
    }
}

Write-Host ""
Write-Host ("release 页面：{0}" -f $release.html_url) -ForegroundColor Cyan
