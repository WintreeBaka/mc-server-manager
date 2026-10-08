<#
    Smoke test for the McServerManager backend (mcsm-cli).

    It exercises every code path that does not need a running Docker daemon:
    environment probing, schema/version metadata, manual install, config read /
    apply / expert / backup / rollback, world archiving, plugin listing and the
    start-failure diagnostics. Everything happens inside -DataRoot.

    Usage:
        .\tools\smoke-test.ps1
        .\tools\smoke-test.ps1 -DataRoot D:\tmp\mcsm-smoke -KeepData
#>
param(
    [string]$Exe = ".\build\bin\mcsm-cli.exe",
    [string]$DataRoot = ".\smoke-data",
    [string]$QtBin = "D:\qt\6.11.2\mingw_64\bin",
    [string]$MinGwBin = "D:\qt\Tools\mingw1310_64\bin",
    [switch]$KeepData
)

$ErrorActionPreference = "Stop"
$script:passed = 0
$script:failed = 0
$script:notes = @()

function Write-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) {
        $script:passed++
        Write-Host ("  [PASS] " + $Name) -ForegroundColor Green
    } else {
        $script:failed++
        Write-Host ("  [FAIL] " + $Name + " -> " + $Detail) -ForegroundColor Red
    }
    if ($Detail -and $Ok) { Write-Verbose "    $Detail" }
}

# Runs the CLI and returns the parsed JSON envelope (never throws).
function Invoke-Cli {
    param([string[]]$Arguments, [switch]$AllowFailure)
    $raw = & $Exe --home $DataRoot @Arguments 2>&1
    $code = $LASTEXITCODE
    $text = ($raw | Out-String).Trim()
    $json = $null
    foreach ($line in ($text -split "`r?`n")) {
        $trimmed = $line.Trim()
        if ($trimmed.StartsWith("{")) {
            try { $json = $trimmed | ConvertFrom-Json } catch { }
        }
    }
    if (-not $json) {
        if (-not $AllowFailure) { throw "no JSON envelope from: mcsm-cli $($Arguments -join ' ')`n$text" }
        return [pscustomobject]@{ ok = $false; exitCode = $code; data = $null; error = @{ message = $text } }
    }
    $json | Add-Member -NotePropertyName exitCode -NotePropertyValue $code -Force
    return $json
}

if (-not (Test-Path $Exe)) { throw "backend not found: $Exe (build first)" }
$Exe = (Resolve-Path $Exe).Path

if (Test-Path $DataRoot) {
    if ($KeepData) {
        Write-Host "reusing data root $DataRoot"
    } else {
        Write-Host "cleaning previous data root $DataRoot"
        Remove-Item -LiteralPath $DataRoot -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $DataRoot -Force | Out-Null
$DataRoot = (Resolve-Path $DataRoot).Path

if ($QtBin -and (Test-Path $QtBin)) { $env:PATH = "$QtBin;$env:PATH" }
if ($MinGwBin -and (Test-Path $MinGwBin)) { $env:PATH = "$MinGwBin;$env:PATH" }

Write-Host ""
Write-Host "McServerManager backend smoke test" -ForegroundColor Cyan
Write-Host "  backend : $Exe"
Write-Host "  data    : $DataRoot"
Write-Host ""

# ---------------------------------------------------------------- metadata ---
Write-Host "metadata" -ForegroundColor Cyan
$version = Invoke-Cli @("version")
Write-Result "version" ($version.ok -and $version.data.name -eq "mcsm-cli") $version.data.home

$types = Invoke-Cli @("types")
Write-Result "types" ($types.ok -and $types.data.types.Count -ge 4) ("types=" + $types.data.types.Count)

$schema = Invoke-Cli @("config", "schema")
$fieldCount = $schema.data.fields.Count
Write-Result "config schema" ($schema.ok -and $fieldCount -ge 30) ("fields=" + $fieldCount)

$doctor = Invoke-Cli @("doctor")
$dockerRunning = [bool]$doctor.data.docker.daemonRunning
Write-Result "doctor" ($doctor.ok -and $null -ne $doctor.data.host) `
    ("docker=" + $(if ($dockerRunning) { "running" } else { "not running (ok for smoke test)" }))
$script:notes += "Docker daemon running: $dockerRunning"

$listEmpty = Invoke-Cli @("server", "list")
Write-Result "server list (empty)" ($listEmpty.ok -and $listEmpty.data.count -eq 0) `
    ("count=" + $listEmpty.data.count)

# ------------------------------------------------------------ manual setup ---
Write-Host ""
Write-Host "manual install (no docker)" -ForegroundColor Cyan
$assets = Join-Path $DataRoot "assets"
New-Item -ItemType Directory -Path $assets -Force | Out-Null
$fakeJar = Join-Path $assets "fake-server.jar"
[System.IO.File]::WriteAllBytes($fakeJar, (New-Object byte[] 8192))

$install = Invoke-Cli @(
    "install",
    "--name", "smoke server",
    "--type", "paper",
    "--manual", "true",
    "--jar", $fakeJar,
    "--image", "eclipse-temurin:17-jre",
    "--port", "25599",
    "--memory", "2G",
    "--level-name", "world",
    "--eula", "true",
    "--skip-docker"
)
Write-Result "install (manual, skip-docker)" $install.ok $install.error.message
$serverId = $install.data.id
if (-not $serverId) { throw "install did not return an id" }
$serverDir = Join-Path $DataRoot ("servers\" + $serverId)

$serverJson = Join-Path $serverDir "server.json"
$jarName = $install.data.jarName
$requiredFiles = @($jarName, "server.properties", "eula.txt", "start.sh", "docker-compose.yml",
                   "README-MCSM.txt")
$missingFiles = $requiredFiles | Where-Object { -not (Test-Path (Join-Path $serverDir $_)) }
Write-Result "generated files" ($missingFiles.Count -eq 0) ("missing=" + ($missingFiles -join ","))

$startScript = Get-Content (Join-Path $serverDir "start.sh") -Raw
$hasCrLf = $startScript.Contains("`r`n")
Write-Result "start.sh uses LF endings" (-not $hasCrLf) "CRLF found"

$detail = Invoke-Cli @("server", "get", "--id", $serverId)
Write-Result "server get" ($detail.ok -and $detail.data.status -eq "stopped") $detail.error.message

# ----------------------------------------------------------------- config ----
Write-Host ""
Write-Host "configuration" -ForegroundColor Cyan
$read = Invoke-Cli @("config", "read", "--id", $serverId)
Write-Result "config read" ($read.ok -and $read.data.values.motd) $read.error.message
Write-Result "config managed rcon" ($read.data.values.'enable-rcon' -eq "true") `
    ("enable-rcon=" + $read.data.values.'enable-rcon')

$valuesFile = Join-Path $assets "values.json"
[System.IO.File]::WriteAllText($valuesFile,
    '{"motd":"smoke test","max-players":"42","view-distance":"8"}',
    (New-Object System.Text.UTF8Encoding($false)))
$apply = Invoke-Cli @(
    "config", "apply", "--id", $serverId,
    "--values-file", $valuesFile,
    "--note", "smoke apply"
)
Write-Result "config apply reports only real changes" ($apply.ok -and $apply.data.count -eq 3) `
    ("count=" + $apply.data.count + " " + $apply.error.message)

$rawAfterApply = Invoke-Cli @("config", "read", "--id", $serverId)
Write-Result "values persisted" ($rawAfterApply.data.values.motd -eq "smoke test" `
        -and $rawAfterApply.data.values.'max-players' -eq "42") `
    ("motd=" + $rawAfterApply.data.values.motd)

$properties = Get-Content (Join-Path $serverDir "server.properties") -Raw
Write-Result "comments preserved" ($properties -match "#") "no comment line survived"

$expertText = @"
# expert mode edit
motd=expert mode
max-players=30
view-distance=6
"@
$expertFile = Join-Path $assets "server.properties.expert"
[System.IO.File]::WriteAllText($expertFile, $expertText, (New-Object System.Text.UTF8Encoding($false)))
$expert = Invoke-Cli @("config", "raw", "--id", $serverId, "--file", $expertFile, "--note", "smoke expert")
Write-Result "config raw (expert)" $expert.ok $expert.error.message

$validate = Invoke-Cli @("config", "validate", "--id", $serverId)
Write-Result "config validate" ($validate.ok) $validate.error.message

$backups = Invoke-Cli @("config", "backups", "--id", $serverId)
Write-Result "config backups recorded" ($backups.ok -and $backups.data.count -ge 2) `
    ("count=" + $backups.data.count)

$rollbackId = $backups.data.backups[0].id
$rollback = Invoke-Cli @("config", "rollback", "--id", $serverId, "--backup", $rollbackId)
Write-Result "config rollback" ($rollback.ok) $rollback.error.message
$afterRollback = Invoke-Cli @("config", "read", "--id", $serverId)
Write-Result "rollback restored previous value" ($afterRollback.data.values.'max-players' -eq "20") `
    ("max-players=" + $afterRollback.data.values.'max-players')

# ----------------------------------------------------------------- backup ----
Write-Host ""
Write-Host "world backup" -ForegroundColor Cyan
$worldDir = Join-Path $serverDir "world"
New-Item -ItemType Directory -Path $worldDir -Force | Out-Null
[System.IO.File]::WriteAllText((Join-Path $worldDir "level.dat"), "smoke level data")
New-Item -ItemType Directory -Path (Join-Path $serverDir "plugins") -Force | Out-Null
[System.IO.File]::WriteAllText((Join-Path $serverDir "plugins\FakePlugin.jar"), "fake plugin")

$backupCreate = Invoke-Cli @("backup", "create", "--id", $serverId, "--note", "smoke backup")
Write-Result "backup create" $backupCreate.ok ($backupCreate.error.message + " " + $backupCreate.error.detail)
$backupName = $backupCreate.data.name

if ($backupName) {
    $archivePath = Join-Path $serverDir ("backups\" + $backupName)
    $archiveOk = (Test-Path $archivePath) -and ((Get-Item $archivePath).Length -gt 100)
    Write-Result "archive exists" $archiveOk ("size=" + (Get-Item $archivePath).Length)
}

$backupList = Invoke-Cli @("backup", "list", "--id", $serverId)
Write-Result "backup list" ($backupList.ok -and $backupList.data.count -ge 1) `
    ("count=" + $backupList.data.count)

$schedule = Invoke-Cli @("backup", "schedule", "--id", $serverId, "--enabled", "true",
                        "--interval", "10", "--keep", "3")
Write-Result "backup schedule" ($schedule.ok -and $schedule.data.schedule.enabled) $schedule.error.message

$tick = Invoke-Cli @("schedule", "tick")
Write-Result "scheduler tick" ($tick.ok) $tick.error.message

$scheduleStatus = Invoke-Cli @("schedule", "status")
Write-Result "schedule status" ($scheduleStatus.ok -and $scheduleStatus.data.servers.Count -eq 1) `
    $scheduleStatus.error.message

# ----------------------------------------------------------------- plugins ---
Write-Host ""
Write-Host "plugins" -ForegroundColor Cyan
$pluginSources = Invoke-Cli @("plugin", "sources")
Write-Result "plugin sources" ($pluginSources.ok -and $pluginSources.data.sources.Count -eq 3) `
    $pluginSources.error.message

$pluginList = Invoke-Cli @("plugin", "list", "--id", $serverId)
Write-Result "plugin list (local scan)" ($pluginList.ok -and $pluginList.data.count -eq 1) `
    ("count=" + $pluginList.data.count)

$toggle = Invoke-Cli @("plugin", "toggle", "--id", $serverId, "--file", "FakePlugin.jar",
                       "--enabled", "false")
Write-Result "plugin disable" ($toggle.ok) $toggle.error.message
$disabledOk = Test-Path (Join-Path $serverDir "plugins\FakePlugin.jar.disabled")
Write-Result "plugin renamed to .disabled" $disabledOk "file not renamed"

# ------------------------------------------------------- start diagnostics ---
Write-Host ""
Write-Host "start failure diagnostics" -ForegroundColor Cyan
$logs = Invoke-Cli @("server", "logs", "--id", $serverId, "--tail", "50")
Write-Result "server logs (no container yet)" ($logs.ok) $logs.error.message

$start = Invoke-Cli @("server", "start", "--id", $serverId, "--wait", "5") -AllowFailure
if ($dockerRunning) {
    Write-Result "start with fake jar reports an error" (-not $start.ok) "unexpectedly succeeded"
} else {
    $code = $start.error.code
    Write-Result "start without docker fails clearly" `
        ((-not $start.ok) -and ($code -eq "DOCKER_UNAVAILABLE" -or $code -eq "SERVER_START_FAILED")) `
        ("code=" + $code + " message=" + $start.error.message)
    Write-Result "failure carries a hint" ([bool]$start.data.hint -or [bool]$start.data.dockerUnavailable) `
        "no hint/dockerUnavailable in payload"
}

$missing = Invoke-Cli @("server", "get", "--id", "does-not-exist") -AllowFailure
Write-Result "unknown server returns NOT_FOUND" ((-not $missing.ok) -and $missing.error.code -eq "NOT_FOUND") `
    ("code=" + $missing.error.code)

$badAction = Invoke-Cli @("server", "explode", "--id", $serverId) -AllowFailure
Write-Result "unknown action rejected" ((-not $badAction.ok) -and $badAction.error.code -eq "UNKNOWN_ACTION") `
    ("code=" + $badAction.error.code)

# ------------------------------------------------------- plugin packages ----
# Extension packages: zip import, automatic scope detection, the JSON-line
# backend protocol, enable/disable and uninstall. No Docker and no Node/Python
# required - the probe backend is a .cmd script.
Write-Host ""
Write-Host "plugin packages" -ForegroundColor Cyan

$zipTool = "third_party\7zip\7za.exe"
if (Test-Path $zipTool) {
    # absolute path: a bare "third_party\..." is treated as a module name by PowerShell
    $zipTool = (Resolve-Path $zipTool).Path
} else {
    $systemZip = "C:\Program Files\7-Zip\7z.exe"
    if (Test-Path $systemZip) { $zipTool = $systemZip } else { $zipTool = $null }
}

function New-PluginZip {
    param(
        [string]$Id,
        [string]$Scope,
        [hashtable]$Extra,
        [string]$ZipPath
    )
    if (-not $zipTool) { return $false }
    $stage = Join-Path $Assets "plugin-$Id"
    if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null

    $manifest = [ordered]@{
        id          = $Id
        name        = "Smoke $Id"
        version     = "1.0.0"
        apiVersion  = "1"
        scope       = $Scope
        description = "smoke test plugin"
        author      = "smoke"
        license     = "MIT"
    }
    foreach ($key in $Extra.Keys) { $manifest[$key] = $Extra[$key] }
    $manifest | ConvertTo-Json -Depth 6 |
        Set-Content -Path (Join-Path $stage "plugin.json") -Encoding UTF8

    if ($Extra.Contains("frontend")) {
        New-Item -ItemType Directory -Path (Join-Path $stage "frontend") -Force | Out-Null
        Set-Content -Path (Join-Path $stage "frontend\index.js") `
            -Value 'mcsm.ui.registerPage({ id: "smoke", title: "Smoke" });' -Encoding UTF8
    }
    if ($Extra.Contains("backend")) {
        New-Item -ItemType Directory -Path (Join-Path $stage "backend") -Force | Out-Null
        @(
            '@echo off',
            'set /p REQUEST=',
            'echo {"ok":true,"data":{"probe":true},"patch":{"jvmArgs":["-XX:+UseG1GC"],"note":"smoke"}}'
        ) | Set-Content -Path (Join-Path $stage "backend\probe.cmd") -Encoding ASCII
    }

    if (Test-Path $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    Push-Location $stage
    try { & $zipTool a -tzip $ZipPath "." -y -bso0 -bsp0 | Out-Null } finally { Pop-Location }
    return $true
}

if (-not $zipTool) {
    Write-Result "plugin zip tool available" $false "neither bundled 7za.exe nor 7-Zip found"
} else {
    $frontZip = Join-Path $Assets "smoke-frontend.zip"
    $backZip = Join-Path $Assets "smoke-backend.zip"
    $globalZip = Join-Path $Assets "smoke-global.zip"
    New-PluginZip -Id "smoke.ui" -Scope "auto" `
        -Extra @{ frontend = @{ entry = "frontend/index.js" } } -ZipPath $frontZip | Out-Null
    New-PluginZip -Id "smoke.backend" -Scope "auto" `
        -Extra @{ backend = @{ entry = "backend/probe.cmd"; runtime = "exec" };
                  hooks = @("server.beforeStart") } -ZipPath $backZip | Out-Null
    New-PluginZip -Id "smoke.global" -Scope "auto" `
        -Extra @{ frontend = @{ entry = "frontend/index.js" };
                  backend = @{ entry = "backend/probe.cmd"; runtime = "exec" } } -ZipPath $globalZip | Out-Null

    $inspectFront = Invoke-Cli @("plugin", "package", "inspect", "--zip", $frontZip)
    Write-Result "plugin package inspect (frontend)" ($inspectFront.ok -and $inspectFront.data.scope -eq "frontend") `
        ("scope=" + $inspectFront.data.scope)

    $inspectBack = Invoke-Cli @("plugin", "package", "inspect", "--zip", $backZip)
    Write-Result "plugin package inspect (backend)" ($inspectBack.ok -and $inspectBack.data.scope -eq "backend") `
        ("scope=" + $inspectBack.data.scope)

    $inspectGlobal = Invoke-Cli @("plugin", "package", "inspect", "--zip", $globalZip)
    Write-Result "plugin package inspect auto detects global" `
        ($inspectGlobal.ok -and $inspectGlobal.data.scope -eq "global" -and $inspectGlobal.data.hasWeb -eq $false) `
        ("scope=" + $inspectGlobal.data.scope)

    $api = Invoke-Cli @("plugin", "api")
    Write-Result "plugin api manifest" ($api.ok -and $api.data.apiVersion -eq "1" -and $api.data.scopes.global) `
        $api.error.message

    $installFront = Invoke-Cli @("plugin", "package", "install", "--zip", $frontZip)
    Write-Result "plugin package install (frontend)" `
        ($installFront.ok -and $installFront.data.id -eq "smoke.ui" -and $installFront.data.scope -eq "frontend") `
        $installFront.error.message
    Write-Result "plugin files copied" `
        (Test-Path (Join-Path $DataRoot "plugins\smoke.ui\plugin.json")) `
        "plugin.json missing in the install directory"

    $reinstall = Invoke-Cli @("plugin", "package", "install", "--zip", $frontZip) -AllowFailure
    Write-Result "installing twice is refused" `
        ((-not $reinstall.ok) -and $reinstall.error.code -eq "PLUGIN_EXISTS") ("code=" + $reinstall.error.code)

    $forceInstall = Invoke-Cli @("plugin", "package", "install", "--zip", $frontZip, "--force")
    Write-Result "--force replaces the plugin" ($forceInstall.ok -and $forceInstall.data.replaced -eq $true) `
        $forceInstall.error.message

    $installBack = Invoke-Cli @("plugin", "package", "install", "--zip", $backZip, "--disabled")
    Write-Result "install --disabled keeps it off" ($installBack.ok -and $installBack.data.enabled -eq $false) `
        $installBack.error.message

    $packages = Invoke-Cli @("plugin", "packages")
    Write-Result "plugin packages list" ($packages.ok -and $packages.data.count -eq 2) `
        ("count=" + $packages.data.count)
    Write-Result "registry reports scopes" `
        (($packages.data.plugins | Where-Object { $_.id -eq "smoke.backend" }).scopeLabel.Length -gt 0) `
        "scopeLabel missing"

    $enable = Invoke-Cli @("plugin", "package", "enable", "--name", "smoke.backend")
    Write-Result "plugin package enable" ($enable.ok -and $enable.data.enabled -eq $true) $enable.error.message

    $call = Invoke-Cli @("plugin", "call", "--name", "smoke.backend", "--method", "server.beforeStart") -AllowFailure
    Write-Result "backend plugin JSON-line call" `
        ($call.ok -and $call.data.data.probe -eq $true -and $call.data.patch.jvmArgs[0] -eq "-XX:+UseG1GC") `
        ($call.error.message + " " + $call.error.detail)

    $info = Invoke-Cli @("plugin", "package", "info", "--name", "smoke.backend")
    Write-Result "plugin package info" ($info.ok -and $info.data.launchCommand) $info.error.message

    $disable = Invoke-Cli @("plugin", "package", "disable", "--name", "smoke.backend")
    Write-Result "plugin package disable" ($disable.ok -and $disable.data.enabled -eq $false) $disable.error.message

    $enabledOnly = Invoke-Cli @("plugin", "packages", "--enabled")
    Write-Result "--enabled filters disabled plugins" `
        ($enabledOnly.data.count -eq 1 -and $enabledOnly.data.enabledCount -eq 1) `
        ("count=" + $enabledOnly.data.count)

    $remove = Invoke-Cli @("plugin", "package", "remove", "--name", "smoke.ui")
    Write-Result "plugin package remove" ($remove.ok -and $remove.data.removed -eq "smoke.ui") $remove.error.message
    Write-Result "removed plugin directory is gone" `
        (-not (Test-Path (Join-Path $DataRoot "plugins\smoke.ui"))) "directory still exists"

    $missingPlugin = Invoke-Cli @("plugin", "package", "info", "--name", "smoke.none") -AllowFailure
    Write-Result "unknown plugin returns NOT_FOUND" `
        ((-not $missingPlugin.ok) -and $missingPlugin.error.code -eq "NOT_FOUND") `
        ("code=" + $missingPlugin.error.code)

    $badPackage = Invoke-Cli @("plugin", "package", "install", "--zip", $fakeJar) -AllowFailure
    Write-Result "non zip package rejected" `
        ((-not $badPackage.ok) -and $badPackage.error.code -eq "INVALID_PLUGIN_PACKAGE") `
        ("code=" + $badPackage.error.code)
}

# ----------------------------------------------------------------- cleanup ----
Write-Host ""
Write-Host "cleanup" -ForegroundColor Cyan
$delete = Invoke-Cli @("server", "delete", "--id", $serverId, "--purge", "true")
Write-Result "server delete" ($delete.ok) $delete.error.message
Write-Result "server directory removed" (-not (Test-Path $serverDir)) "directory still exists"

$finalList = Invoke-Cli @("server", "list")
Write-Result "server list empty again" ($finalList.data.count -eq 0) ("count=" + $finalList.data.count)

Write-Host ""
Write-Host ("passed: " + $script:passed + "   failed: " + $script:failed) `
    -ForegroundColor $(if ($script:failed -eq 0) { "Green" } else { "Red" })
foreach ($note in $script:notes) { Write-Host ("note: " + $note) -ForegroundColor DarkGray }

if (-not $KeepData) {
    Remove-Item -LiteralPath $DataRoot -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "removed smoke data root"
} else {
    Write-Host "kept smoke data root: $DataRoot"
}

exit $script:failed
