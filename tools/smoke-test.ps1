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
