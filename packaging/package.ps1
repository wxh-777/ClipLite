param(
    [string]$Configuration = "Release",
    [string]$Version = "1.2.1"
)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-x64\$Configuration\ClipLite.exe"
$outRoot = Join-Path $root "out"
$portableOut = Join-Path $outRoot "ClipLite-$Version-portable-win-x64"
$portableZip = Join-Path $outRoot "ClipLite-$Version-portable-win-x64.zip"
$checksumsOut = Join-Path $outRoot "SHA256SUMS-$Version.txt"
$installerOut = Join-Path $outRoot "installer"

if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

if (Test-Path $portableOut) {
    Remove-Item $portableOut -Recurse -Force
}
if (Test-Path $portableZip) {
    Remove-Item $portableZip -Force
}
New-Item -ItemType Directory -Path $portableOut | Out-Null
New-Item -ItemType Directory -Path (Join-Path $portableOut "data") | Out-Null
New-Item -ItemType File -Path (Join-Path $portableOut "portable.flag") | Out-Null
Copy-Item $exe (Join-Path $portableOut "ClipLite.exe")
Copy-Item (Join-Path $root "README.md") $portableOut
Copy-Item (Join-Path $root "README.en.md") $portableOut
Copy-Item (Join-Path $root "CHANGELOG.md") $portableOut
Copy-Item (Join-Path $root "CHANGELOG.en.md") $portableOut
Copy-Item (Join-Path $root "LICENSE.md") $portableOut
Copy-Item (Join-Path $root "CONTRIBUTING.md") $portableOut
Copy-Item (Join-Path $root "CONTRIBUTING.en.md") $portableOut
Copy-Item (Join-Path $root "SECURITY.md") $portableOut
Copy-Item (Join-Path $root "SECURITY.en.md") $portableOut
$hash = (Get-FileHash (Join-Path $portableOut "ClipLite.exe") -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -Path (Join-Path $portableOut "SHA256SUM.txt") -Value "$hash  ClipLite.exe" -Encoding ASCII
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $portableOut, $portableZip, [System.IO.Compression.CompressionLevel]::Optimal, $false)

$iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
$userInnoHome = [Environment]::GetEnvironmentVariable("INNO_SETUP_HOME", "User")
$isccPath = @(
    if ($iscc) { $iscc.Source }
    if ($userInnoHome) { Join-Path $userInnoHome "ISCC.exe" }
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe")
    (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($isccPath) {
    if (Test-Path $installerOut) {
        Remove-Item $installerOut -Recurse -Force
    }
    New-Item -ItemType Directory -Path $installerOut | Out-Null
    & $isccPath "/DAppVersion=$Version" "/DSourceRoot=$root" "/DOutputDir=$installerOut" (Join-Path $PSScriptRoot "ClipLite.iss")
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup failed with exit code $LASTEXITCODE"
    }
} else {
    Write-Warning "ISCC.exe was not found. Portable package was created; install Inno Setup to build the normal installer."
}

$checksumFiles = @($portableZip)
$installerFile = Join-Path $installerOut "ClipLite-Setup-$Version-x64.exe"
if (Test-Path $installerFile) {
    $checksumFiles += $installerFile
}
$checksumLines = foreach ($file in $checksumFiles) {
    $fileHash = (Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant()
    "$fileHash  $(Split-Path $file -Leaf)"
}
Set-Content -Path $checksumsOut -Value $checksumLines -Encoding ASCII

Get-ChildItem $outRoot -Recurse -File | Select-Object FullName,Length
