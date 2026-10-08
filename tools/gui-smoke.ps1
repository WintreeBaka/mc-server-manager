<#
    GUI smoke test: starts the desktop app, verifies it stays alive and that the
    main window is really on screen, optionally captures the app window (only
    that window, never the whole desktop) and then closes it.

    Usage:
        .\tools\gui-smoke.ps1
        .\tools\gui-smoke.ps1 -Screenshot .\smoke-shot.png -Seconds 10
        .\tools\gui-smoke.ps1 -Page settings-runtime -Resize 1100x700 -Screenshot .\small.png

    -Resize needs -Screenshot (the Win32 helpers are only loaded for capture).
#>
param(
    [string]$Exe = ".\build\bin\McServerManager.exe",
    [string]$QtBin = "D:\qt\6.11.2\mingw_64\bin",
    [string]$MinGwBin = "D:\qt\Tools\mingw1310_64\bin",
    [int]$Seconds = 9,
    [string]$Screenshot = "",
    [string]$Page = "",
    [string]$Resize = "",
    [switch]$TestDrag,
    [switch]$TestPageSwitch,
    [switch]$KeepOpen
)

$ErrorActionPreference = "Stop"
$failed = 0

function Write-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) {
        Write-Host ("  [PASS] " + $Name) -ForegroundColor Green
    } else {
        $script:failed++
        Write-Host ("  [FAIL] " + $Name + " -> " + $Detail) -ForegroundColor Red
    }
}

if (-not (Test-Path $Exe)) { throw "GUI executable not found: $Exe (build first)" }
$Exe = (Resolve-Path $Exe).Path

if ($QtBin -and (Test-Path $QtBin)) {
    $env:PATH = "$QtBin;$env:PATH"
} elseif ($QtBin) {
    Write-Warning "Qt bin not found: $QtBin"
}
if ($MinGwBin -and (Test-Path $MinGwBin)) { $env:PATH = "$MinGwBin;$env:PATH" }

Write-Host ""
Write-Host "McServerManager GUI smoke test" -ForegroundColor Cyan
Write-Host "  desktop : $Exe"
Write-Host ""

$arguments = @()
if ($Page) { $arguments += @("--page", $Page) }
if ($arguments.Count -gt 0) {
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru
} else {
    $process = Start-Process -FilePath $Exe -PassThru
}

Start-Sleep -Seconds 3
Write-Result "process is alive after 3s" (-not $process.HasExited) `
    ("exit code " + $process.ExitCode)

$handle = [IntPtr]::Zero
for ($i = 0; $i -lt 20 -and $handle -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 400
    $process.Refresh()
    if ($process.HasExited) { break }
    $handle = $process.MainWindowHandle
}
Write-Result "main window created" ($handle -ne [IntPtr]::Zero) "no MainWindowHandle"
Write-Result "window has a title" (-not [string]::IsNullOrWhiteSpace($process.MainWindowTitle)) `
    "empty MainWindowTitle"

Start-Sleep -Seconds ([Math]::Max(0, $Seconds - 4))
$process.Refresh()
Write-Result ("process alive after " + $Seconds + "s") (-not $process.HasExited) `
    ("exit code " + $process.ExitCode)

if ($process.HasExited) {
    Write-Host "the app exited early; no screenshot taken" -ForegroundColor Red
    exit $failed
}

if ($Screenshot -and $handle -ne [IntPtr]::Zero) {
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class WinApi {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, IntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@

    # geometry of the window in screen coordinates (used by both test blocks)
    $windowRect = New-Object WinApi+RECT
    [void][WinApi]::GetWindowRect($handle, [ref]$windowRect)

    if ($Resize -match '^(\d+)x(\d+)$') {
        # shrink the window first: layout regressions (menus scrolling away,
        # overlapping cards) only show up in small windows
        $targetW = [int]$Matches[1]
        $targetH = [int]$Matches[2]
        [void][WinApi]::SetWindowPos($handle, [IntPtr]::Zero, $windowRect.Left, $windowRect.Top,
                                     $targetW, $targetH, 0x0004)
        Start-Sleep -Milliseconds 1200
        Write-Result ("window resized to " + $targetW + "x" + $targetH) $true
    }

    if ($TestDrag) {
        $before = New-Object WinApi+RECT
        [void][WinApi]::GetWindowRect($handle, [ref]$before)
        # press in the empty middle part of the title bar and drag the window
        $startX = $before.Left + [int](($before.Right - $before.Left) * 0.62)
        $startY = $before.Top + 28
        [void][WinApi]::SetCursorPos($startX, $startY)
        Start-Sleep -Milliseconds 250
        [WinApi]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
        Start-Sleep -Milliseconds 150
        for ($step = 1; $step -le 12; $step++) {
            [void][WinApi]::SetCursorPos($startX + 12 * $step, $startY + 9 * $step)
            Start-Sleep -Milliseconds 30
        }
        [WinApi]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
        Start-Sleep -Milliseconds 500

        $after = New-Object WinApi+RECT
        [void][WinApi]::GetWindowRect($handle, [ref]$after)
        $dx = $after.Left - $before.Left
        $dy = $after.Top - $before.Top
        Write-Result "window can be dragged by the title bar" (($dx -gt 40) -and ($dy -gt 30)) `
            ("moved by $dx,$dy (expected about 144,108)")
        # put it back so the screenshot geometry stays stable
        [void][WinApi]::SetWindowPos($handle, [IntPtr]::Zero, $before.Left, $before.Top, 0, 0, 0x0015)
        Start-Sleep -Milliseconds 300
    }

    if ($TestPageSwitch) {
        # Ctrl+1..Ctrl+7 switch pages; press them faster than the transition runs
        [void][WinApi]::SetForegroundWindow($handle)
        Start-Sleep -Milliseconds 300
        foreach ($page in @(2, 4, 5, 6, 3, 7, 4, 2)) {
            [WinApi]::keybd_event(0x11, 0, 0, [IntPtr]::Zero)              # Ctrl down
            Start-Sleep -Milliseconds 25
            [WinApi]::keybd_event([byte](0x30 + $page), 0, 0, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 25
            [WinApi]::keybd_event([byte](0x30 + $page), 0, 0x0002, [IntPtr]::Zero)
            [WinApi]::keybd_event(0x11, 0, 0x0002, [IntPtr]::Zero)         # Ctrl up
            Start-Sleep -Milliseconds 60
        }
        Start-Sleep -Milliseconds 1500
        Write-Host "         switched 8 times rapidly; the landing page must show no leftovers" -ForegroundColor DarkGray
    }

    [void][WinApi]::ShowWindow($handle, 9)      # SW_RESTORE
    [void][WinApi]::SetWindowPos($handle, [IntPtr]::Zero, 0, 0, 0, 0, 0x0043)  # top, no move/resize
    [void][WinApi]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds 1200

    $rect = New-Object WinApi+RECT
    [void][WinApi]::GetWindowRect($handle, [ref]$rect)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -gt 100 -and $height -gt 100) {
        $bitmap = New-Object System.Drawing.Bitmap $width, $height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        # PrintWindow renders the target window itself, so the capture does not
        # depend on window stacking (a background app cannot steal the foreground).
        $hdc = $graphics.GetHdc()
        $printed = [WinApi]::PrintWindow($handle, $hdc, 2)   # PW_RENDERFULLCONTENT
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()

        if ($printed) {
            # a fully black capture means PrintWindow was not supported: fall back
            $samples = @($bitmap.GetPixel([int]($width / 2), [int]($height / 2)),
                         $bitmap.GetPixel([int]($width / 4), [int]($height / 3)),
                         $bitmap.GetPixel([int]($width / 3), [int]($height / 2)))
            $blank = ($samples | Where-Object { $_.R -ne 0 -or $_.G -ne 0 -or $_.B -ne 0 }).Count -eq 0
            if ($blank) {
                Write-Host "         PrintWindow returned a blank image, using screen capture" -ForegroundColor DarkGray
                $printed = $false
            }
        }
        if (-not $printed) {
            $graphics2 = [System.Drawing.Graphics]::FromImage($bitmap)
            $graphics2.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
            $graphics2.Dispose()
        }
        $screenshotPath = [System.IO.Path]::GetFullPath($Screenshot)
        $bitmap.Save($screenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
        Write-Result "window screenshot captured" (Test-Path $screenshotPath) $screenshotPath
        Write-Host ("         -> " + $screenshotPath) -ForegroundColor DarkGray
    } else {
        Write-Result "window screenshot captured" $false ("window is " + $width + "x" + $height)
    }
}

if ($KeepOpen) {
    Write-Host "leaving the application open (PID $($process.Id))" -ForegroundColor Yellow
    exit $failed
}

Write-Host "closing the application..." -ForegroundColor DarkGray
if (-not $process.CloseMainWindow()) { $process.Kill() }
if (-not $process.WaitForExit(8000)) {
    $process.Kill()
    $process.WaitForExit(5000)
}
Write-Result "closed cleanly" $process.HasExited "still running"

# the app starts a scheduler daemon; it must not outlive the window. Killing it
# takes a moment, so give it a few seconds before deciding.
$leftover = $null
for ($attempt = 0; $attempt -lt 10; $attempt++) {
    $leftover = Get-Process -Name "mcsm-cli" -ErrorAction SilentlyContinue
    if ($null -eq $leftover) { break }
    Start-Sleep -Milliseconds 600
}
Write-Result "no leftover backend daemon" ($null -eq $leftover) `
    ("still running: " + (($leftover | Measure-Object).Count))

Write-Host ""
if ($failed -eq 0) {
    Write-Host "GUI smoke test passed" -ForegroundColor Green
} else {
    Write-Host ("GUI smoke test failed: " + $failed) -ForegroundColor Red
}
exit $failed
