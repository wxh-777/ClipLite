param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [int]$Iterations = 100
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class ClipLiteStressNative {
    private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    private static extern int GetClassName(IntPtr hwnd, StringBuilder name, int max);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr h, uint message, IntPtr w, IntPtr l);

    public static IntPtr FindWindowForProcess(uint processId, string expectedClass) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((hwnd, lParam) => {
            uint ownerProcessId;
            GetWindowThreadProcessId(hwnd, out ownerProcessId);
            if (ownerProcessId != processId || !IsWindowVisible(hwnd)) return true;
            var className = new StringBuilder(256);
            GetClassName(hwnd, className, className.Capacity);
            if (string.Equals(className.ToString(), expectedClass, StringComparison.Ordinal)) {
                result = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
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

function Invoke-ClipLiteHistory([System.Diagnostics.Process]$process, [int]$timeoutMs) {
    $forwarder = Start-Process -FilePath $resolved -ArgumentList "--history" -PassThru
    $deadline = [Environment]::TickCount64 + $timeoutMs
    while ([Environment]::TickCount64 -lt $deadline) {
        if ($process.HasExited) { break }
        $popup = [ClipLiteStressNative]::FindWindowForProcess([uint32]$process.Id, "ClipLitePopup")
        if ($popup -ne [IntPtr]::Zero) {
            if (!$forwarder.HasExited) { $forwarder.WaitForExit(1000) | Out-Null }
            return $popup
        }
        Start-Sleep -Milliseconds 10
    }
    if (!$forwarder.HasExited) { Stop-Process -Id $forwarder.Id -Force -ErrorAction SilentlyContinue }
    return [IntPtr]::Zero
}

function Get-ClipLitePopup([System.Diagnostics.Process]$process) {
    return [ClipLiteStressNative]::FindWindowForProcess([uint32]$process.Id, "ClipLitePopup")
}

try {
    $process = Start-Process -FilePath $resolved -PassThru
    $popup = Invoke-ClipLiteHistory $process 10000
    if ($process.HasExited -or $popup -eq [IntPtr]::Zero) {
        throw "ClipLite history window did not become ready (exited=$($process.HasExited))."
    }
    for ($i = 0; $i -lt $Iterations; ++$i) {
        $popup = Get-ClipLitePopup $process
        if ($popup -ne [IntPtr]::Zero) {
            if (![ClipLiteStressNative]::PostMessage($popup, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) {
                throw "Unable to close the history window on iteration $i."
            }
            for ($wait = 0; $wait -lt 100 -and
                (Get-ClipLitePopup $process) -ne [IntPtr]::Zero; ++$wait) {
                Start-Sleep -Milliseconds 20
            }
            if ((Get-ClipLitePopup $process) -ne [IntPtr]::Zero) {
                throw "History window did not close on iteration $i."
            }
        }
        $popup = Invoke-ClipLiteHistory $process 2000
        if ($popup -eq [IntPtr]::Zero) {
            throw "History window did not open on iteration $i."
        }
    }
    $exitForwarder = Start-Process -FilePath $resolved -ArgumentList "--exit" -PassThru
    if (!$exitForwarder.WaitForExit(6000)) {
        Stop-Process -Id $exitForwarder.Id -Force -ErrorAction SilentlyContinue
    }
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
