param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [int]$Iterations = 100
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ClipLiteStressNative {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string cls, string title);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr h, uint message, IntPtr w, IntPtr l);
}
'@

$candidate = if ([IO.Path]::IsPathRooted($ExePath)) { $ExePath } else { Join-Path $PSScriptRoot $ExePath }
$resolved = (Resolve-Path $candidate).Path
if (Get-Process -Name ClipLite -ErrorAction SilentlyContinue) {
    throw "Close existing ClipLite instances before running the stress test."
}
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ("ClipLiteStress-" + [guid]::NewGuid().ToString("N"))
$previousDataDirectory = $env:CLIPLITE_TEST_DATA_DIR
New-Item -ItemType Directory -Path $testDirectory -ErrorAction Stop | Out-Null
$env:CLIPLITE_TEST_DATA_DIR = $testDirectory

function Invoke-ClipLiteCommand([string]$arguments, [int]$timeoutMs) {
    $forwarder = Start-Process -FilePath $resolved -ArgumentList $arguments -PassThru
    if (!$forwarder.WaitForExit($timeoutMs)) {
        Stop-Process -Id $forwarder.Id -Force -ErrorAction SilentlyContinue
    }
}

try {
    $process = Start-Process -FilePath $resolved -PassThru
    for ($wait = 0; $wait -lt 500 -and !$process.HasExited -and
        [ClipLiteStressNative]::FindWindowEx([IntPtr]::new(-3), [IntPtr]::Zero,
            "ClipLiteHidden", $null) -eq [IntPtr]::Zero; ++$wait) {
        Start-Sleep -Milliseconds 20
    }
    if ($process.HasExited -or [ClipLiteStressNative]::FindWindowEx(
        [IntPtr]::new(-3), [IntPtr]::Zero, "ClipLiteHidden", $null) -eq [IntPtr]::Zero) {
        throw "ClipLite message window did not become ready (exited=$($process.HasExited))."
    }
    for ($i = 0; $i -lt $Iterations; ++$i) {
        $popup = [ClipLiteStressNative]::FindWindow("ClipLitePopup", $null)
        if ($popup -ne [IntPtr]::Zero) {
            if (![ClipLiteStressNative]::PostMessage($popup, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) {
                throw "Unable to close the history window on iteration $i."
            }
            for ($wait = 0; $wait -lt 100 -and
                [ClipLiteStressNative]::FindWindow("ClipLitePopup", $null) -ne [IntPtr]::Zero; ++$wait) {
                Start-Sleep -Milliseconds 20
            }
            if ([ClipLiteStressNative]::FindWindow("ClipLitePopup", $null) -ne [IntPtr]::Zero) {
                throw "History window did not close on iteration $i."
            }
        }
        Invoke-ClipLiteCommand "--history" 2000
        for ($wait = 0; $wait -lt 100 -and
            [ClipLiteStressNative]::FindWindow("ClipLitePopup", $null) -eq [IntPtr]::Zero; ++$wait) {
            Start-Sleep -Milliseconds 20
        }
        if ([ClipLiteStressNative]::FindWindow("ClipLitePopup", $null) -eq [IntPtr]::Zero) {
            throw "History window did not open on iteration $i."
        }
    }
    Invoke-ClipLiteCommand "--exit" 6000
    if (!$process.WaitForExit(6000)) {
        throw "ClipLite did not exit after the stress test."
    }
    [PSCustomObject]@{ Iterations = $Iterations; Completed = $true }
} finally {
    if ($process -and (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $process.Id -Force
    }
    $env:CLIPLITE_TEST_DATA_DIR = $previousDataDirectory
    Remove-Item -LiteralPath $testDirectory -Recurse -Force -ErrorAction Stop
}
