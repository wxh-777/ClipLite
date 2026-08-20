param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [int]$Iterations = 10000,
    [string]$TextPrefix = "ClipLite stress"
)

$candidate = if ([IO.Path]::IsPathRooted($ExePath)) { $ExePath } else { Join-Path $PSScriptRoot $ExePath }
$resolved = (Resolve-Path $candidate).Path
$startedProcess = $null
$originalText = $null
$canRestore = $false

function Set-ClipboardTextWithRetry([string]$value) {
    for ($attempt = 0; $attempt -lt 10; ++$attempt) {
        try {
            Set-Clipboard -Value $value -ErrorAction Stop
            return
        } catch {
            Start-Sleep -Milliseconds 20
        }
    }
    throw "Clipboard remained locked after retrying."
}

try {
    if (Get-Command Get-Clipboard -ErrorAction SilentlyContinue) {
        try {
            $originalText = Get-Clipboard -Raw -Format Text -ErrorAction Stop
            $canRestore = $true
        } catch {
            $canRestore = $false
        }
    } else {
        throw "Get-Clipboard/Set-Clipboard is unavailable in this PowerShell session."
    }

    $existing = Get-Process -Name ClipLite -ErrorAction SilentlyContinue
    if (-not $existing) {
        $startedProcess = Start-Process -FilePath $resolved -PassThru
        Start-Sleep -Milliseconds 500
    }

    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt $Iterations; ++$i) {
        Set-ClipboardTextWithRetry ("{0} {1}" -f $TextPrefix, $i)
    }
    $stopwatch.Stop()

    [PSCustomObject]@{
        Iterations = $Iterations
        ElapsedMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
        Completed = $true
    }
} finally {
    if ($canRestore) {
        try {
            Set-ClipboardTextWithRetry $originalText
        } catch {
            Write-Warning "Unable to restore the original text clipboard: $($_.Exception.Message)"
        }
    }
    if ($startedProcess -and (Get-Process -Id $startedProcess.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $startedProcess.Id -Force
    }
}
