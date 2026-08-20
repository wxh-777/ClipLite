param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [int]$WaitMilliseconds = 1000
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ClipLiteResourceNative {
    [DllImport("user32.dll")]
    public static extern uint GetGuiResources(IntPtr process, uint flags);
}
'@

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
    $sample = Get-Process -Id $process.Id
    $file = Get-Item $resolvedExe
    $gdiObjects = [ClipLiteResourceNative]::GetGuiResources($sample.Handle, 0)
    $userObjects = [ClipLiteResourceNative]::GetGuiResources($sample.Handle, 1)
    [PSCustomObject]@{
        PID = $sample.Id
        ProcessStartMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
        WorkingSetMB = [math]::Round($sample.WorkingSet64 / 1MB, 2)
        PrivateMB = [math]::Round($sample.PrivateMemorySize64 / 1MB, 2)
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
