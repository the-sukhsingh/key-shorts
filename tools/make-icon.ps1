$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$outPath = Join-Path $PSScriptRoot "..\assets\keyshorts.ico"
$sizes = @(16, 24, 32, 48, 64, 128)
$blobs = @()

foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $pad = [Math]::Max(1, [int]($s * 0.03))
    $r = [Math]::Max(2, [int]($s * 0.22))
    $w = $s - 2 * $pad
    $h = $w
    $bgPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $bgPath.AddArc($pad, $pad, $d, $d, 180, 90)
    $bgPath.AddArc($pad + $w - $d, $pad, $d, $d, 270, 90)
    $bgPath.AddArc($pad + $w - $d, $pad + $h - $d, $d, $d, 0, 90)
    $bgPath.AddArc($pad, $pad + $h - $d, $d, $d, 90, 90)
    $bgPath.CloseFigure()
    $bgBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 17, 24, 39))
    $g.FillPath($bgBrush, $bgPath)

    $kx = [float]($s * 0.19)
    $ky = [float]($s * 0.28)
    $kw = [float]($s * 0.62)
    $kh = [float]($s * 0.44)
    $kr = [float][Math]::Max(1.5, $s * 0.08)
    $keyPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $dk = 2 * $kr
    $keyPath.AddArc($kx, $ky, $dk, $dk, 180, 90)
    $keyPath.AddArc($kx + $kw - $dk, $ky, $dk, $dk, 270, 90)
    $keyPath.AddArc($kx + $kw - $dk, $ky + $kh - $dk, $dk, $dk, 0, 90)
    $keyPath.AddArc($kx, $ky + $kh - $dk, $dk, $dk, 90, 90)
    $keyPath.CloseFigure()
    $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 248, 250, 252))
    $g.FillPath($white, $keyPath)

    $accent = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 37, 99, 235))
    $barH = [float][Math]::Max(1.0, $s * 0.075)
    $g.FillRectangle($accent, [float]($kx + $kw * 0.16), [float]($ky + $kh - $barH - $s * 0.09), [float]($kw * 0.68), $barH)

    if ($s -ge 24) {
        $font = New-Object System.Drawing.Font("Segoe UI", [float]($s * 0.30), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        $fmt = New-Object System.Drawing.StringFormat
        $fmt.Alignment = [System.Drawing.StringAlignment]::Center
        $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
        $textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 17, 24, 39))
        $rect = New-Object System.Drawing.RectangleF([float]$kx, [float]($ky - $s * 0.02), [float]$kw, [float]($kh - $s * 0.06))
        $g.DrawString("K", $font, $textBrush, $rect, $fmt)
        $font.Dispose(); $fmt.Dispose(); $textBrush.Dispose()
    }

    $g.Dispose()

    $rect = New-Object System.Drawing.Rectangle(0, 0, $s, $s)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $bytes = New-Object byte[] ($stride * $s)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    $bmp.Dispose()

    $maskRow = [int]([Math]::Ceiling($s / 32.0) * 4)
    $maskSize = $maskRow * $s
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    $bw.Write([UInt32]40)
    $bw.Write([Int32]$s)
    $bw.Write([Int32]($s * 2))
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]0)
    $bw.Write([UInt32]($s * $s * 4 + $maskSize))
    $bw.Write([Int32]0)
    $bw.Write([Int32]0)
    $bw.Write([UInt32]0)
    $bw.Write([UInt32]0)
    for ($y = $s - 1; $y -ge 0; $y--) {
        $bw.Write($bytes, $y * $stride, $s * 4)
    }
    $bw.Write((New-Object byte[] $maskSize), 0, $maskSize)
    $bw.Flush()
    $blobs += ,$ms.ToArray()
    $bw.Dispose(); $ms.Dispose()
}

$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($out)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $bw.Write([byte]$s)
    $bw.Write([byte]$s)
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$blobs[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $blobs[$i].Length
}
foreach ($blob in $blobs) { $bw.Write($blob) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($outPath, $out.ToArray())
$bw.Dispose(); $out.Dispose()
Write-Host "Wrote $outPath ($((Get-Item $outPath).Length) bytes)"
