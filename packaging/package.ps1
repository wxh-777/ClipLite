param(
    [string]$Configuration = "Release",
    [string]$Version = "0.1.0"
)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-x64\$Configuration\ClipLite.exe"
$out = Join-Path $root "out\ClipLite-$Version-win-x64"

if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

if (Test-Path $out) {
    Remove-Item $out -Recurse -Force
}
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item $exe (Join-Path $out "ClipLite.exe")
Copy-Item (Join-Path $root "README.md") $out
Copy-Item (Join-Path $root "CHANGELOG.md") $out

$hash = (Get-FileHash (Join-Path $out "ClipLite.exe") -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -Path (Join-Path $out "SHA256SUM.txt") -Value "$hash  ClipLite.exe" -Encoding ASCII
Get-ChildItem $out | Select-Object Name,Length
