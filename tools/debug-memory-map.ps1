param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [int]$WaitMilliseconds = 1200
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ClipLiteMemoryMapNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORY_BASIC_INFORMATION {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public UIntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern UIntPtr VirtualQueryEx(IntPtr process, IntPtr address,
        out MEMORY_BASIC_INFORMATION information, UIntPtr length);
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
    $process = Start-Process -FilePath $resolvedExe -ArgumentList "--settings" -PassThru
    Start-Sleep -Milliseconds $WaitMilliseconds
    $process.Refresh()
    $infoSize = [UIntPtr]::new([System.Runtime.InteropServices.Marshal]::SizeOf(
        [type][ClipLiteMemoryMapNative+MEMORY_BASIC_INFORMATION]))
    $address = [IntPtr]::Zero
    $regions = [System.Collections.Generic.List[object]]::new()
    while ($true) {
        $info = New-Object ClipLiteMemoryMapNative+MEMORY_BASIC_INFORMATION
        $result = [ClipLiteMemoryMapNative]::VirtualQueryEx($process.Handle, $address,
            [ref]$info, $infoSize)
        if ($result -eq [UIntPtr]::Zero) { break }
        $size = $info.RegionSize.ToUInt64()
        if ($info.State -eq 0x1000 -and $size -gt 0) {
            $type = switch ($info.Type) {
                0x1000000 { "IMAGE"; break }
                0x40000 { "MAPPED"; break }
                0x20000 { "PRIVATE"; break }
                default { "OTHER" }
            }
            $regions.Add([PSCustomObject]@{
                Type = $type
                Base = "0x{0:X}" -f $info.BaseAddress.ToInt64()
                SizeMB = [math]::Round($size / 1MB, 3)
            })
        }
        $next = $info.BaseAddress.ToInt64() + [long]$size
        if ($next -le $address.ToInt64()) { break }
        $address = [IntPtr]::new($next)
    }
    $regions | Group-Object Type | ForEach-Object {
        [PSCustomObject]@{
            Type = $_.Name
            Regions = $_.Count
            SizeMB = [math]::Round((($_.Group | Measure-Object SizeMB -Sum).Sum), 3)
        }
    }
    "Top private regions:"
    $regions | Where-Object Type -eq "PRIVATE" | Sort-Object SizeMB -Descending | Select-Object -First 12
} finally {
    if ($process -and (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $process.Id -Force
    }
}
