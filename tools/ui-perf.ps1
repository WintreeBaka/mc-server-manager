<#
    界面性能测量：分别测「空闲」与「快速切换页面」时的 CPU 占用。

    用法：
        .\tools\ui-perf.ps1
        .\tools\ui-perf.ps1 -Seconds 12 -IdleSeconds 4 -Mode mixed

    -Mode cycle : 依次快速切过全部页面（Ctrl+1..7，最接近"疯狂点菜单"）
    -Mode pages : 只在主菜单页与设置页之间来回切（Ctrl+2 / Ctrl+7）
    -Mode mixed : cycle + 点击设置页左侧菜单项

    数值含义：被测进程占用「全部逻辑核心」的百分比
    （例如 12 核机器上 10% ≈ 1.2 个核心满载）。
#>
param(
    [string]$Exe = ".\build\bin\McServerManager.exe",
    [string]$QtBin = "D:\qt\6.11.2\mingw_64\bin",
    [int]$IdleSeconds = 4,
    [int]$Seconds = 12,
    [int]$IntervalMs = 260,
    [int]$WarmupSeconds = 10,
    [string]$Resize = "",
    [ValidateSet("cycle", "pages", "mixed")]
    [string]$Mode = "cycle",
    [switch]$KeepOpen
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Exe)) { throw "GUI executable not found: $Exe" }
$Exe = (Resolve-Path $Exe).Path
if ($QtBin -and (Test-Path $QtBin)) { $env:PATH = "$QtBin;$env:PATH" }

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class PerfWinApi {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, IntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@

function Get-CpuPercent {
    param([System.Diagnostics.Process]$Process, [double]$ElapsedSeconds)
    $cpu = $Process.TotalProcessorTime.TotalSeconds
    $cores = [Environment]::ProcessorCount
    return [math]::Round(100.0 * $cpu / ($ElapsedSeconds * $cores), 2)
}

function Send-CtrlKey {
    param([int]$Digit)
    [PerfWinApi]::keybd_event(0x11, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 12
    [PerfWinApi]::keybd_event([byte](0x30 + $Digit), 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 12
    [PerfWinApi]::keybd_event([byte](0x30 + $Digit), 0, 0x0002, [IntPtr]::Zero)
    [PerfWinApi]::keybd_event(0x11, 0, 0x0002, [IntPtr]::Zero)
}

function Send-Click {
    param([int]$X, [int]$Y)
    [void][PerfWinApi]::SetCursorPos($X, $Y)
    Start-Sleep -Milliseconds 40
    [PerfWinApi]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 30
    [PerfWinApi]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
}

Write-Host ""
Write-Host "McServerManager UI performance" -ForegroundColor Cyan
Write-Host "  desktop : $Exe"
Write-Host "  mode    : $Mode   interval: ${IntervalMs}ms"
Write-Host ""

$process = Start-Process -FilePath $Exe -PassThru
$handle = [IntPtr]::Zero
for ($i = 0; $i -lt 25 -and $handle -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 400
    $process.Refresh()
    if ($process.HasExited) { throw "the app exited during startup" }
    $handle = $process.MainWindowHandle
}
if ($handle -eq [IntPtr]::Zero) { throw "no main window" }

# 预热：等启动期的 doctor / server list / 插件加载全部结束，否则会把启动开销
# 算进"空闲"基线里
Start-Sleep -Seconds $WarmupSeconds
[void][PerfWinApi]::ShowWindow($handle, 9)
[void][PerfWinApi]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds 800

if ($Resize -match '^(\d+)x(\d+)$') {
    $rect0 = New-Object PerfWinApi+RECT
    [void][PerfWinApi]::GetWindowRect($handle, [ref]$rect0)
    [void][PerfWinApi]::SetWindowPos($handle, [IntPtr]::Zero, $rect0.Left, $rect0.Top,
                                     [int]$Matches[1], [int]$Matches[2], 0x0004)
    Start-Sleep -Milliseconds 1000
    Write-Host ("窗口尺寸 : " + $Matches[1] + "x" + $Matches[2]) -ForegroundColor DarkGray
}

$process.Refresh()
$idleStart = $process.TotalProcessorTime.TotalSeconds
Start-Sleep -Seconds ([math]::Max(1, [int]($IdleSeconds / 2)))
$process.Refresh()
$idleMid = $process.TotalProcessorTime.TotalSeconds
Start-Sleep -Seconds ([math]::Max(1, $IdleSeconds - [int]($IdleSeconds / 2)))
$process.Refresh()
$idleEnd = $process.TotalProcessorTime.TotalSeconds
$half = [math]::Max(1, [int]($IdleSeconds / 2))
$idle1 = [math]::Round(100.0 * ($idleMid - $idleStart) / ($half * [Environment]::ProcessorCount), 2)
$idle2 = [math]::Round(100.0 * ($idleEnd - $idleMid) / (($IdleSeconds - $half) * [Environment]::ProcessorCount), 2)
$idle = [math]::Round(100.0 * ($idleEnd - $idleStart) / ($IdleSeconds * [Environment]::ProcessorCount), 2)
Write-Host ("空闲 {0}s      : {1}% CPU  (前 {2}s={3}% / 后 {4}s={5}%)" -f $IdleSeconds, $idle, $half, $idle1, ($IdleSeconds - $half), $idle2) -ForegroundColor DarkGray

$rect = New-Object PerfWinApi+RECT
[void][PerfWinApi]::GetWindowRect($handle, [ref]$rect)
$navY = @(173, 221, 269, 317)   # 设置页左侧四个菜单项（窗口相对坐标）

$process.Refresh()
$busyStart = $process.TotalProcessorTime.TotalSeconds
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$switches = 0
$navIndex = 0
$pageDigit = 1
while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
    if ($Mode -eq "pages") {
        # 主菜单页 <-> 设置页
        Send-CtrlKey -Digit 2
        Start-Sleep -Milliseconds $IntervalMs
        Send-CtrlKey -Digit 7
        $switches++
        Start-Sleep -Milliseconds $IntervalMs
        continue
    }
    Send-CtrlKey -Digit $pageDigit
    $switches++
    $pageDigit++
    if ($pageDigit -gt 7) { $pageDigit = 1 }
    Start-Sleep -Milliseconds $IntervalMs
    if ($Mode -eq "mixed" -and $pageDigit -eq 1) {
        Send-Click -X ($rect.Left + 130) -Y ($rect.Top + $navY[$navIndex % $navY.Count])
        $navIndex++
    }
    Start-Sleep -Milliseconds $IntervalMs
}
$watch.Stop()
$process.Refresh()
$busyEnd = $process.TotalProcessorTime.TotalSeconds
$busy = [math]::Round(100.0 * ($busyEnd - $busyStart) / ($watch.Elapsed.TotalSeconds * [Environment]::ProcessorCount), 2)

Write-Host ("切换 {0} 次 / {1:N1}s : {2}% CPU" -f $switches, $watch.Elapsed.TotalSeconds, $busy) `
    -ForegroundColor Yellow

if (-not $KeepOpen) {
    if (-not $process.CloseMainWindow()) { $process.Kill() }
    if (-not $process.WaitForExit(8000)) { $process.Kill() }
}

Write-Host ""
Write-Host ("RESULT idle={0}%  switch={1}%  switches={2}" -f $idle, $busy, $switches) -ForegroundColor Cyan
exit 0
