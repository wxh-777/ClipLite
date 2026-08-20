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
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr h, uint message, IntPtr w, IntPtr l);
}
'@

$candidate = if ([IO.Path]::IsPathRooted($ExePath)) { $ExePath } else { Join-Path $PSScriptRoot $ExePath }
$resolved = (Resolve-Path $candidate).Path
$process = Start-Process -FilePath $resolved -PassThru
try {
    Start-Sleep -Milliseconds 400
    for ($i = 0; $i -lt $Iterations; ++$i) {
        $popup = [ClipLiteStressNative]::FindWindow("ClipLitePopup", $null)
        if ($popup -ne [IntPtr]::Zero) {
            [ClipLiteStressNative]::PostMessage($popup, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        }
        Start-Process -FilePath $resolved -ArgumentList "--history" | Out-Null
        Start-Sleep -Milliseconds 20
    }
    Start-Process -FilePath $resolved -ArgumentList "--exit" | Out-Null
    Start-Sleep -Milliseconds 400
    [PSCustomObject]@{ Iterations = $Iterations; Completed = $true }
} finally {
    if (Get-Process -Id $process.Id -ErrorAction SilentlyContinue) {
        Stop-Process -Id $process.Id -Force
    }
}
