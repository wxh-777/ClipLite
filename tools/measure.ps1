param(
    [string]$ExePath = "..\build\Release\ClipLite.exe",
    [int]$WaitMilliseconds = 1000
)

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
    [PSCustomObject]@{
        PID = $sample.Id
        ProcessStartMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
        WorkingSetMB = [math]::Round($sample.WorkingSet64 / 1MB, 2)
        PrivateMB = [math]::Round($sample.PrivateMemorySize64 / 1MB, 2)
        ExecutableKB = [math]::Round($file.Length / 1KB, 1)
    }
} finally {
    if ($process -and (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $process.Id -Force
    }
}
