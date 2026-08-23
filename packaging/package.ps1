param(
    [string]$Configuration = "Release",
    [string]$Version = "0.1.0"
)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-x64\$Configuration\ClipLite.exe"
$outRoot = Join-Path $root "out"
$portableOut = Join-Path $outRoot "ClipLite-$Version-portable-win-x64"
$installerOut = Join-Path $outRoot "installer"

if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

if (Test-Path $portableOut) {
    Remove-Item $portableOut -Recurse -Force
}
New-Item -ItemType Directory -Path $portableOut | Out-Null
New-Item -ItemType Directory -Path (Join-Path $portableOut "data") | Out-Null
New-Item -ItemType File -Path (Join-Path $portableOut "portable.flag") | Out-Null
Copy-Item $exe (Join-Path $portableOut "ClipLite.exe")
Copy-Item (Join-Path $root "README.md") $portableOut
Copy-Item (Join-Path $root "CHANGELOG.md") $portableOut
Copy-Item (Join-Path $root "resources\support-wechat.png") $portableOut
Copy-Item (Join-Path $root "resources\support-alipay.png") $portableOut
Copy-Item (Join-Path $root "resources\support-qq.jpg") $portableOut

$hash = (Get-FileHash (Join-Path $portableOut "ClipLite.exe") -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -Path (Join-Path $portableOut "SHA256SUM.txt") -Value "$hash  ClipLite.exe" -Encoding ASCII

$iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
$userInnoHome = [Environment]::GetEnvironmentVariable("INNO_SETUP_HOME", "User")
$isccPath = @(
    if ($iscc) { $iscc.Source }
    if ($userInnoHome) { Join-Path $userInnoHome "ISCC.exe" }
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

Get-ChildItem $outRoot -Recurse -File | Select-Object FullName,Length
