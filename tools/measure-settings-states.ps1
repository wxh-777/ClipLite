param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [int]$WaitMilliseconds = 1200
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ClipLiteStateMeasureNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct ProcessMemoryCounters {
        public uint cb;
        public uint PageFaultCount;
        public UIntPtr PeakWorkingSetSize;
        public UIntPtr WorkingSetSize;
        public UIntPtr QuotaPeakPagedPoolUsage;
        public UIntPtr QuotaPagedPoolUsage;
        public UIntPtr QuotaPeakNonPagedPoolUsage;
        public UIntPtr QuotaNonPagedPoolUsage;
        public UIntPtr PagefileUsage;
        public UIntPtr PeakPagefileUsage;
        public UIntPtr PrivateUsage;
    }

    [DllImport("psapi.dll", SetLastError = true)]
    public static extern bool GetProcessMemoryInfo(IntPtr process,
        out ProcessMemoryCounters counters, uint size);

    [DllImport("user32.dll")]
    public static extern uint GetGuiResources(IntPtr process, uint flags);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hwnd, uint message,
        UIntPtr wParam, IntPtr lParam);

    public static long CommitBytes(IntPtr process) {
        ProcessMemoryCounters counters;
        counters.cb = (uint)Marshal.SizeOf(typeof(ProcessMemoryCounters));
        if (!GetProcessMemoryInfo(process, out counters, counters.cb)) return 0;
        return (long)counters.PagefileUsage.ToUInt64();
    }
}
'@

function Get-State([Diagnostics.Process]$Process, [string]$Name) {
    $Process.Refresh()
    [PSCustomObject]@{
        State = $Name
        PID = $Process.Id
        WorkingSetMB = [math]::Round($Process.WorkingSet64 / 1MB, 2)
        PrivateMB = [math]::Round($Process.PrivateMemorySize64 / 1MB, 2)
        CommitMB = [math]::Round([ClipLiteStateMeasureNative]::CommitBytes($Process.Handle) / 1MB, 2)
        CpuMs = [math]::Round($Process.TotalProcessorTime.TotalMilliseconds, 2)
        Handles = $Process.HandleCount
        GdiObjects = [ClipLiteStateMeasureNative]::GetGuiResources($Process.Handle, 0)
        UserObjects = [ClipLiteStateMeasureNative]::GetGuiResources($Process.Handle, 1)
    }
}

$candidatePath = if ([IO.Path]::IsPathRooted($ExePath)) {
    $ExePath
} else {
    Join-Path $PSScriptRoot $ExePath
}
$resolvedExe = (Resolve-Path $candidatePath).Path
$process = $null
try {
    $process = Start-Process -FilePath $resolvedExe -ArgumentList "--settings" -PassThru
    Start-Sleep -Milliseconds $WaitMilliseconds
    Get-State $process "SettingsOpen"
    [ClipLiteStateMeasureNative]::PostMessage($process.MainWindowHandle, 0x0010,
        [UIntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 800
    Get-State $process "SettingsClosed"
} finally {
    if ($process -and (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $process.Id -Force
    }
}
