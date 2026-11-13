[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)][string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [string]$Executable = '',
    [int]$ObservationSeconds = 8,
    [switch]$RequireLauncherTransition
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $ProjectDir 'x64\Release\OmniGhost.exe'
}
$Executable = [IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "OmniGhost executable not found: $Executable"
}
if ($ObservationSeconds -lt 3 -or $ObservationSeconds -gt 60) {
    throw 'ObservationSeconds must be between 3 and 60.'
}

if (-not ('OmniGhostWindowProbe' -as [type])) {
    Add-Type @'
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
public static class OmniGhostWindowProbe {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr parameter);
    public delegate void WinEventProc(IntPtr hook, uint eventType, IntPtr hwnd, int objectId, int childId, uint eventThread, uint eventTime);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, System.Text.StringBuilder value, int maximum);
    [DllImport("user32.dll")] static extern IntPtr SetWinEventHook(uint eventMin, uint eventMax, IntPtr module, WinEventProc callback, uint processId, uint threadId, uint flags);
    [DllImport("user32.dll")] static extern bool UnhookWinEvent(IntPtr hook);
    [DllImport("user32.dll")] static extern int GetMessage(out NativeMessage message, IntPtr hwnd, uint min, uint max);
    [DllImport("user32.dll")] static extern bool PostThreadMessage(uint threadId, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [StructLayout(LayoutKind.Sequential)] struct NativeMessage { public IntPtr hwnd; public uint message; public UIntPtr wParam; public IntPtr lParam; public uint time; public int x; public int y; public uint lPrivate; }
    const uint EVENT_OBJECT_SHOW = 0x8002;
    const uint WINEVENT_OUTOFCONTEXT = 0;
    const int OBJID_WINDOW = 0;
    static readonly ConcurrentDictionary<long, string> shown = new ConcurrentDictionary<long, string>();
    static WinEventProc callback;
    static IntPtr eventHook;
    static Thread captureThread;
    static uint captureThreadId;
    static volatile uint targetPid;

    public static void StartCapture() {
        shown.Clear();
        targetPid = 0;
        callback = OnWindowEvent;
        var ready = new ManualResetEventSlim(false);
        Exception startupFailure = null;
        captureThread = new Thread(() => {
            captureThreadId = GetCurrentThreadId();
            eventHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, IntPtr.Zero, callback, 0, 0, WINEVENT_OUTOFCONTEXT);
            if (eventHook == IntPtr.Zero) startupFailure = new InvalidOperationException("SetWinEventHook failed.");
            ready.Set();
            if (startupFailure == null) {
                NativeMessage message;
                while (GetMessage(out message, IntPtr.Zero, 0, 0) > 0) { }
                UnhookWinEvent(eventHook);
                eventHook = IntPtr.Zero;
            }
        });
        captureThread.IsBackground = true;
        captureThread.Name = "OmniGhost startup window event capture";
        captureThread.Start();
        ready.Wait();
        if (startupFailure != null) throw startupFailure;
    }
    public static void SetTargetPid(uint pid) { targetPid = pid; }
    public static void StopCapture() {
        if (captureThread != null) {
            PostThreadMessage(captureThreadId, 0x0012, IntPtr.Zero, IntPtr.Zero);
            captureThread.Join(2000);
            captureThread = null;
        }
        callback = null;
    }
    static void OnWindowEvent(IntPtr hook, uint eventType, IntPtr hwnd, int objectId, int childId, uint eventThread, uint eventTime) {
        if (hwnd == IntPtr.Zero || objectId != OBJID_WINDOW || childId != 0) return;
        uint pid; GetWindowThreadProcessId(hwnd, out pid);
        bool belongs = pid == targetPid && targetPid != 0;
        if (!belongs && targetPid == 0) {
            try { belongs = String.Equals(Process.GetProcessById((int)pid).ProcessName, "OmniGhost", StringComparison.OrdinalIgnoreCase); }
            catch { belongs = false; }
        }
        if (!belongs) return;
        var cls = new System.Text.StringBuilder(256);
        GetClassName(hwnd, cls, cls.Capacity);
        shown.TryAdd(hwnd.ToInt64(), hwnd.ToInt64().ToString("X") + "|" + cls.ToString());
    }
    public static string[] ShownWindows() { return new List<string>(shown.Values).ToArray(); }
    public static string[] VisibleWindows(uint targetPid) {
        var result = new List<string>();
        EnumWindows((hwnd, _) => {
            uint pid; GetWindowThreadProcessId(hwnd, out pid);
            if (pid == targetPid && IsWindowVisible(hwnd)) {
                var cls = new System.Text.StringBuilder(256);
                GetClassName(hwnd, cls, cls.Capacity);
                result.Add(hwnd.ToInt64().ToString("X") + "|" + cls.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }
}
'@
}

$existing = @(Get-Process -Name OmniGhost -ErrorAction SilentlyContinue)
if ($existing.Count -ne 0) {
    throw 'Close every existing OmniGhost process before running the startup regression test.'
}

$log = Join-Path $env:LOCALAPPDATA 'OmniGhost\logs.txt'
if (-not (Test-Path -LiteralPath $log)) {
    $log = Join-Path $env:LOCALAPPDATA 'OmniGhost\logs\logs.txt'
}
$logStartLength = if (Test-Path -LiteralPath $log) {
    (Get-Item -LiteralPath $log).Length
} else { 0L }

[OmniGhostWindowProbe]::StartCapture()
$process = Start-Process -FilePath $Executable -WorkingDirectory (Split-Path -Parent $Executable) -PassThru
[OmniGhostWindowProbe]::SetTargetPid([uint32]$process.Id)
$samples = New-Object Collections.Generic.List[object]
$allHandles = New-Object Collections.Generic.HashSet[string]([StringComparer]::OrdinalIgnoreCase)
$maximumProcesses = 0
$maximumWindows = 0
$deadline = [DateTime]::UtcNow.AddSeconds($ObservationSeconds)
try {
    while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        $instances = @(Get-Process -Name OmniGhost -ErrorAction SilentlyContinue)
        $maximumProcesses = [Math]::Max($maximumProcesses, $instances.Count)
        $windows = @([OmniGhostWindowProbe]::VisibleWindows([uint32]$process.Id))
        $maximumWindows = [Math]::Max($maximumWindows, $windows.Count)
        foreach ($window in $windows) { [void]$allHandles.Add(($window -split '\|',2)[0]) }
        $samples.Add([pscustomobject]@{
            utc = [DateTime]::UtcNow.ToString('o')
            processCount = $instances.Count
            visibleWindows = @($windows)
        })
        Start-Sleep -Milliseconds 2
    }

    if ($process.HasExited) { throw "Primary OmniGhost exited early with code $($process.ExitCode)." }
    $second = Start-Process -FilePath $Executable -WorkingDirectory (Split-Path -Parent $Executable) -PassThru
    if (-not $second.WaitForExit(10000)) {
        try { $second.Kill() } catch {}
        throw 'Second launch did not exit within 10 seconds.'
    }
    if ($second.ExitCode -ne 3) { throw "Second launch returned $($second.ExitCode), expected 3." }
    if ($maximumProcesses -gt 1) { throw "More than one concurrent OmniGhost process was observed: $maximumProcesses" }
    if ($maximumWindows -gt 1) { throw "More than one visible top-level window was observed in one sample: $maximumWindows" }
    if ($allHandles.Count -gt 1) { throw "The visible HWND changed during observation: $($allHandles -join ', ')" }
    $shownWindows = @([OmniGhostWindowProbe]::ShownWindows())
    $shownHandles = @($shownWindows | ForEach-Object { ($_ -split '\|', 2)[0] } | Sort-Object -Unique)
    if ($shownHandles.Count -gt 1) {
        throw "More than one top-level HWND emitted a SHOW event, including transient windows: $($shownWindows -join ', ')"
    }

    $launcherTransition = $false
    if (Test-Path -LiteralPath $log) {
        $stream = [IO.File]::Open($log, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            if ($stream.Length -ge $logStartLength) { [void]$stream.Seek($logStartLength, [IO.SeekOrigin]::Begin) }
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true, 4096, $true)
            try { $content = $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
        $launcherTransition = $content -match 'after-launcher-transition' -and
            $content -match 'same HWND retained'
    }
    if ($RequireLauncherTransition -and -not $launcherTransition) {
        throw 'Launcher transition was not observed. Complete authentication during the test window.'
    }

    $result = [ordered]@{
        schemaVersion = 2
        executable = $Executable
        samples = $samples.Count
        maximumConcurrentProcesses = $maximumProcesses
        maximumVisibleWindows = $maximumWindows
        distinctVisibleHwnd = $allHandles.Count
        distinctShownHwnd = $shownHandles.Count
        shownWindowEvents = $shownWindows
        secondLaunchExitCode = 3
        launcherTransition = if ($launcherTransition) { 'PASS' } else { 'NOT_TESTED' }
        result = 'PASS'
    }
    $output = Join-Path $ProjectDir 'artifacts\validation\startup-window-regression.json'
    New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $output -Encoding UTF8
    Write-Host "[StartupWindow] PASS samples=$($samples.Count) max_processes=$maximumProcesses max_windows=$maximumWindows hwnd_count=$($allHandles.Count) second_exit=3 launcher=$($result.launcherTransition)"
    Write-Host "[StartupWindow] report=$output"
}
finally {
    [OmniGhostWindowProbe]::StopCapture()
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
}
