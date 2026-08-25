if ($PSVersionTable.PSVersion.Major -lt 7) {
    throw "PowerShell 7 or later is required. Run this script with pwsh.exe."
}

Add-Type -AssemblyName System.Drawing.Common

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root "docs\screenshots\history.png"
$iconPath = Join-Path $root "resources\clipLite.ico"
$outputPath = Join-Path $root "docs\promo\wechat-cover.png"

function U([int[]]$codes) {
    return -join ($codes | ForEach-Object { [char]$_ })
}

function New-RoundedPath([System.Drawing.RectangleF]$rect, [float]$radius) {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $radius * 2
    $arc = [System.Drawing.RectangleF]::new($rect.X, $rect.Y, $diameter, $diameter)
    $path.AddArc($arc, 180, 90)
    $arc.X = $rect.Right - $diameter
    $path.AddArc($arc, 270, 90)
    $arc.Y = $rect.Bottom - $diameter
    $path.AddArc($arc, 0, 90)
    $arc.X = $rect.X
    $path.AddArc($arc, 90, 90)
    $path.CloseFigure()
    return $path
}

$title = "535 KB " + (U @(0x7684, 0x526A, 0x8D34, 0x677F, 0x5386, 0x53F2))
$subtitle = (U @(0x540E, 0x53F0, 0x5E38, 0x9A7B, 0x5185, 0x5B58, 0x7EA6)) + " 2.4 MB"
$persistent = U @(0x91CD, 0x542F, 0x7535, 0x8111, 0xFF0C, 0x5386, 0x53F2, 0x4ECD, 0x5728)
$features = (U @(0x641C, 0x7D22)) + "  /  " + (U @(0x5206, 0x7C7B)) + "  /  " + (U @(0x7F6E, 0x9876)) + "  /  " + (U @(0x5355, 0x51FB, 0x7C98, 0x8D34))
$winV = "Win+V / Alt+V"

$width = 900
$height = 383
$bitmap = [System.Drawing.Bitmap]::new($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit

$canvas = [System.Drawing.Color]::FromArgb(245, 248, 252)
$paper = [System.Drawing.Color]::FromArgb(255, 255, 255)
$blue = [System.Drawing.Color]::FromArgb(38, 101, 232)
$blueSoft = [System.Drawing.Color]::FromArgb(231, 239, 255)
$ink = [System.Drawing.Color]::FromArgb(19, 35, 56)
$muted = [System.Drawing.Color]::FromArgb(91, 108, 130)
$border = [System.Drawing.Color]::FromArgb(216, 226, 239)
$orange = [System.Drawing.Color]::FromArgb(225, 113, 0)
$left = 60

$graphics.Clear($canvas)

# Stable left content area: x=60..570. Product screenshot area: x=640..860.

$icon = [System.Drawing.Icon]::new($iconPath, 30, 30)
$graphics.DrawIcon($icon, [System.Drawing.Rectangle]::new($left, 38, 30, 30))
$icon.Dispose()

$brandFont = [System.Drawing.Font]::new("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$titleFont = [System.Drawing.Font]::new("Microsoft YaHei UI", 27, [System.Drawing.FontStyle]::Bold)
$subtitleFont = [System.Drawing.Font]::new("Microsoft YaHei UI", 16, [System.Drawing.FontStyle]::Regular)
$bodyFont = [System.Drawing.Font]::new("Microsoft YaHei UI", 12, [System.Drawing.FontStyle]::Regular)
$bodyBoldFont = [System.Drawing.Font]::new("Microsoft YaHei UI", 11, [System.Drawing.FontStyle]::Bold)
$smallFont = [System.Drawing.Font]::new("Microsoft YaHei UI", 10.5, [System.Drawing.FontStyle]::Regular)

$blueBrush = [System.Drawing.SolidBrush]::new($blue)
$inkBrush = [System.Drawing.SolidBrush]::new($ink)
$mutedBrush = [System.Drawing.SolidBrush]::new($muted)
$paperBrush = [System.Drawing.SolidBrush]::new($paper)
$orangeBrush = [System.Drawing.SolidBrush]::new($orange)

$graphics.DrawString("ClipLite", $brandFont, $inkBrush, 104, 42)
$graphics.DrawString("WINDOWS CLIPBOARD HISTORY", $smallFont, $mutedBrush, 224, 47)

# One-line title avoids font-specific CJK line-height collisions.
$graphics.DrawString($title, $titleFont, $inkBrush, 53, 104)
$graphics.FillRectangle($blueBrush, $left, 166, 72, 5)
$graphics.DrawString($subtitle, $subtitleFont, $mutedBrush, 55, 183)

# Two compact facts, kept well inside the left safe area.
$factOneRect = [System.Drawing.RectangleF]::new($left, 247, 246, 50)
$factTwoRect = [System.Drawing.RectangleF]::new(324, 247, 246, 50)
$factOnePath = New-RoundedPath $factOneRect 8
$factTwoPath = New-RoundedPath $factTwoRect 8
$graphics.FillPath([System.Drawing.SolidBrush]::new($blueSoft), $factOnePath)
$graphics.FillPath($paperBrush, $factTwoPath)
$graphics.DrawPath([System.Drawing.Pen]::new($border, 1), $factTwoPath)
$graphics.FillEllipse($blueBrush, 75, 267, 8, 8)
$graphics.FillEllipse($orangeBrush, 339, 267, 8, 8)
$graphics.DrawString($persistent, $bodyBoldFont, $inkBrush, 92, 255)
$graphics.DrawString($winV, $bodyBoldFont, $inkBrush, 356, 255)
$factOnePath.Dispose()
$factTwoPath.Dispose()

$graphics.DrawString($features, $smallFont, $mutedBrush, 56, 329)

# Screenshot frame and shadow. The screenshot itself is already anonymized.
$shadowRect = [System.Drawing.RectangleF]::new(642, 22, 228, 347)
$shadowPath = New-RoundedPath $shadowRect 10
$graphics.FillPath([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(28, 35, 55, 80)), $shadowPath)
$shadowPath.Dispose()

$frameRect = [System.Drawing.RectangleF]::new(632, 12, 228, 347)
$framePath = New-RoundedPath $frameRect 10
$graphics.FillPath($paperBrush, $framePath)
$graphics.DrawPath([System.Drawing.Pen]::new($border, 1), $framePath)
$framePath.Dispose()

$source = [System.Drawing.Image]::FromFile($sourcePath)
$destination = [System.Drawing.Rectangle]::new(640, 20, 212, 331)
$graphics.DrawImage($source, $destination)
$source.Dispose()

$graphics.Dispose()
$bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

Write-Output "Created $outputPath"
