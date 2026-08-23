param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [int]$WaitMilliseconds = 1000
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ClipLiteResourceNative {
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
    [DllImport("user32.dll")]
    public static extern uint GetGuiResources(IntPtr process, uint flags);
    [DllImport("psapi.dll", SetLastError = true)]
    public static extern bool GetProcessMemoryInfo(IntPtr process,
        out ProcessMemoryCounters counters, uint size);
    public static long GetCommitBytes(IntPtr process) {
        ProcessMemoryCounters counters;
        counters.cb = (uint)Marshal.SizeOf(typeof(ProcessMemoryCounters));
        if (!GetProcessMemoryInfo(process, out counters, counters.cb)) return 0;
        return (long)counters.PagefileUsage.ToUInt64();
    }
}
'@

function Get-GpuCounterBytes([int]$ProcessId, [string]$CounterPath) {
    try {
        $samples = (Get-Counter -Counter $CounterPath -ErrorAction Stop).CounterSamples
        $samples |
            Where-Object { $_.InstanceName -match "_$ProcessId$" } |
            Measure-Object -Property CookedValue -Sum |
            Select-Object -ExpandProperty Sum
    } catch {
        $null
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
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $resolvedExe -PassThru
    $stopwatch.Stop()
    Start-Sleep -Milliseconds $WaitMilliseconds
    $sample = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
    if (-not $sample) {
        $process.Refresh()
        Write-Warning "ClipLite exited before sampling. An existing single instance may have handled the launch."
        return
    }
    $file = Get-Item $resolvedExe
    $gdiObjects = [ClipLiteResourceNative]::GetGuiResources($sample.Handle, 0)
    $userObjects = [ClipLiteResourceNative]::GetGuiResources($sample.Handle, 1)
    $dedicatedGpu = Get-GpuCounterBytes $sample.Id '\GPU Process Memory(*)\Dedicated Usage'
    $sharedGpu = Get-GpuCounterBytes $sample.Id '\GPU Process Memory(*)\Shared Usage'
    [PSCustomObject]@{
        PID = $sample.Id
        ProcessStartMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
        WorkingSetMB = [math]::Round($sample.WorkingSet64 / 1MB, 2)
        PrivateMB = [math]::Round($sample.PrivateMemorySize64 / 1MB, 2)
        CommitMB = [math]::Round([ClipLiteResourceNative]::GetCommitBytes($sample.Handle) / 1MB, 2)
        CpuMs = [math]::Round($sample.TotalProcessorTime.TotalMilliseconds, 2)
        GpuDedicatedMB = if ($null -eq $dedicatedGpu) { $null } else { [math]::Round($dedicatedGpu / 1MB, 2) }
        GpuSharedMB = if ($null -eq $sharedGpu) { $null } else { [math]::Round($sharedGpu / 1MB, 2) }
        ExecutableKB = [math]::Round($file.Length / 1KB, 1)
        Handles = $sample.HandleCount
        GdiObjects = $gdiObjects
        UserObjects = $userObjects
    }
} finally {
    if ($process -and (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $process.Id -Force
    }
}
