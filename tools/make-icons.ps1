# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generate ten Game Boy-style icon alternatives as 256px PNGs and multi-size
# ICO files, plus a contact sheet for review.

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$OutDir = Join-Path $ProjectRoot "docs\icons"
New-Item -ItemType Directory -Force $OutDir | Out-Null

Add-Type -AssemblyName System.Drawing

$Darkest  = [System.Drawing.Color]::FromArgb(255, 0x0F, 0x38, 0x0F)
$Dark     = [System.Drawing.Color]::FromArgb(255, 0x30, 0x62, 0x30)
$Light    = [System.Drawing.Color]::FromArgb(255, 0x8B, 0xAC, 0x0F)
$Lightest = [System.Drawing.Color]::FromArgb(255, 0x9B, 0xBC, 0x0F)
$Transparent = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)

function New-Grid {
    $g = New-Object 'char[]' 256
    for ($i = 0; $i -lt 256; $i++) { $g[$i] = '.' }
    return ,$g
}

function Set-Px($g, [int]$x, [int]$y, [char]$c) {
    if ($x -ge 0 -and $x -lt 16 -and $y -ge 0 -and $y -lt 16) {
        $g[$y * 16 + $x] = $c
    }
}

function Fill-Rect($g, [int]$x, [int]$y, [int]$w, [int]$h, [char]$c) {
    for ($yy = $y; $yy -lt $y + $h; $yy++) {
        for ($xx = $x; $xx -lt $x + $w; $xx++) { Set-Px $g $xx $yy $c }
    }
}

function Frame-Rect($g, [int]$x, [int]$y, [int]$w, [int]$h, [char]$c) {
    Fill-Rect $g $x $y $w 1 $c
    Fill-Rect $g $x ($y + $h - 1) $w 1 $c
    Fill-Rect $g $x $y 1 $h $c
    Fill-Rect $g ($x + $w - 1) $y 1 $h $c
}

function Tri-Down($g, [int]$x, [int]$y, [int[]]$widths, [char]$c) {
    for ($i = 0; $i -lt $widths.Count; $i++) {
        Fill-Rect $g ($x + $i) ($y + $i) $widths[$i] 1 $c
    }
}

function Tri-Right($g, [int]$x, [int]$y, [int[]]$widths, [char]$c) {
    for ($i = 0; $i -lt $widths.Count; $i++) {
        Fill-Rect $g $x ($y + $i) $widths[$i] 1 $c
    }
}

function Base-Grid {
    $g = New-Grid
    Fill-Rect $g 1 2 14 12 '0'
    Fill-Rect $g 2 1 12 14 '0'
    Fill-Rect $g 2 2 12 12 '3'
    return ,$g
}

function Icon-Compress {
    $g = Base-Grid
    Fill-Rect $g 7 3 2 5 '0'
    Tri-Down $g 4 8 @(8, 6, 4, 2) '0'
    Fill-Rect $g 4 12 8 1 '0'
    return ,$g
}

function Icon-Play {
    $g = Base-Grid
    Fill-Rect $g 5 4 2 8 '0'
    Tri-Right $g 7 4 @(1, 2, 3, 4, 4, 3, 2, 1) '0'
    return ,$g
}

function Icon-Film {
    $g = Base-Grid
    Frame-Rect $g 3 3 10 10 '0'
    Fill-Rect $g 4 4 8 2 '1'
    Fill-Rect $g 4 10 8 2 '1'
    Fill-Rect $g 4 7 8 2 '2'
    foreach ($y in @(4, 5, 9, 10)) {
        Set-Px $g 3 $y '3'
        Set-Px $g 12 $y '3'
    }
    return ,$g
}

function Icon-Cartridge {
    $g = Base-Grid
    Fill-Rect $g 4 2 8 2 '0'
    Fill-Rect $g 3 4 10 9 '0'
    Fill-Rect $g 4 5 8 7 '3'
    Fill-Rect $g 5 6 6 2 '1'
    Fill-Rect $g 7 8 2 2 '0'
    Tri-Down $g 6 10 @(4, 2) '0'
    return ,$g
}

function Icon-Vhs {
    $g = Base-Grid
    Frame-Rect $g 2 4 12 8 '0'
    Fill-Rect $g 3 5 10 6 '3'
    Fill-Rect $g 4 6 3 3 '1'
    Fill-Rect $g 9 6 3 3 '1'
    Set-Px $g 5 7 '3'
    Set-Px $g 10 7 '3'
    Fill-Rect $g 4 10 8 1 '0'
    return ,$g
}

function Icon-Folder {
    $g = Base-Grid
    Fill-Rect $g 2 4 5 2 '0'
    Fill-Rect $g 3 5 3 1 '3'
    Fill-Rect $g 2 6 12 7 '0'
    Fill-Rect $g 3 7 10 5 '3'
    Tri-Right $g 6 8 @(1, 2, 3, 3, 2, 1) '0'
    return ,$g
}

function Icon-FF {
    $g = Base-Grid
    Fill-Rect $g 4 4 2 8 '0'
    Fill-Rect $g 4 4 5 2 '0'
    Fill-Rect $g 4 7 4 2 '0'
    Fill-Rect $g 9 4 2 8 '0'
    Fill-Rect $g 9 4 5 2 '0'
    Fill-Rect $g 9 7 4 2 '0'
    return ,$g
}

function Icon-Handheld {
    $g = Base-Grid
    Fill-Rect $g 3 2 10 12 '0'
    Fill-Rect $g 4 3 8 10 '3'
    Fill-Rect $g 4 3 8 4 '1'
    Fill-Rect $g 5 4 6 3 '3'
    Fill-Rect $g 6 5 4 1 '2'
    Fill-Rect $g 4 9 2 1 '0'
    Fill-Rect $g 5 8 1 3 '0'
    Fill-Rect $g 10 9 1 2 '0'
    Fill-Rect $g 11 8 1 1 '0'
    Fill-Rect $g 5 12 6 1 '1'
    Fill-Rect $g 5 13 6 1 '1'
    return ,$g
}

function Icon-Disc {
    $g = Base-Grid
    $rows = @(
        @(6, 3, 4), @(4, 4, 2), @(10, 4, 2), @(3, 5, 2), @(11, 5, 2),
        @(3, 6, 1), @(12, 6, 1), @(3, 7, 1), @(12, 7, 1),
        @(3, 8, 1), @(12, 8, 1), @(3, 9, 2), @(11, 9, 2),
        @(4, 10, 2), @(10, 10, 2), @(6, 11, 4)
    )
    foreach ($r in $rows) { Fill-Rect $g $r[0] $r[1] $r[2] 1 '0' }
    Fill-Rect $g 7 7 2 2 '0'
    Set-Px $g 7 7 '3'
    return ,$g
}

function Icon-Press {
    $g = Base-Grid
    Fill-Rect $g 4 3 8 2 '0'
    Fill-Rect $g 3 5 10 2 '0'
    Fill-Rect $g 5 7 6 1 '1'
    Frame-Rect $g 3 9 10 4 '0'
    Fill-Rect $g 4 10 8 2 '2'
    return ,$g
}

function Grid-To-Bitmap($g, [int]$size) {
    $colors = @{
        '.' = $Transparent
        '0' = $Darkest
        '1' = $Dark
        '2' = $Light
        '3' = $Lightest
    }
    $src = New-Object System.Drawing.Bitmap 16, 16
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $src.SetPixel($x, $y, $colors[[string]$g[$y * 16 + $x]])
        }
    }
    $dst = New-Object System.Drawing.Bitmap $size, $size
    $graphics = [System.Drawing.Graphics]::FromImage($dst)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.DrawImage($src, 0, 0, $size, $size)
    $graphics.Dispose()
    $src.Dispose()
    return $dst
}

function Save-Ico($g, [string]$path) {
    $sizes = @(16, 32, 48, 64, 128, 256)
    $pngs = New-Object 'System.Collections.Generic.List[byte[]]'
    foreach ($size in $sizes) {
        $bmp = Grid-To-Bitmap $g $size
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $pngs.Add($ms.ToArray())
        $ms.Dispose()
        $bmp.Dispose()
    }
    $fs = [System.IO.File]::Create($path)
    $bw = New-Object System.IO.BinaryWriter($fs)
    try {
        $bw.Write([UInt16]0)
        $bw.Write([UInt16]1)
        $bw.Write([UInt16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $size = $sizes[$i]
            $dim = if ($size -eq 256) { 0 } else { $size }
            $bw.Write([byte]$dim)
            $bw.Write([byte]$dim)
            $bw.Write([byte]0)
            $bw.Write([byte]0)
            $bw.Write([UInt16]1)
            $bw.Write([UInt16]32)
            $bw.Write([UInt32]$pngs[$i].Length)
            $bw.Write([UInt32]$offset)
            $offset += $pngs[$i].Length
        }
        foreach ($png in $pngs) { $bw.Write($png) }
    } finally {
        $bw.Dispose()
        $fs.Dispose()
    }
}

$icons = @(
    @{ Name = "compress";  Label = "COMPRESS";  Grid = (Icon-Compress) },
    @{ Name = "play";      Label = "PLAY";      Grid = (Icon-Play) },
    @{ Name = "film";      Label = "FILM";      Grid = (Icon-Film) },
    @{ Name = "cartridge"; Label = "CARTRIDGE"; Grid = (Icon-Cartridge) },
    @{ Name = "vhs";       Label = "VHS";       Grid = (Icon-Vhs) },
    @{ Name = "folder";    Label = "FOLDER";    Grid = (Icon-Folder) },
    @{ Name = "ff-mark";   Label = "FF MARK";   Grid = (Icon-FF) },
    @{ Name = "handheld";  Label = "HANDHELD";  Grid = (Icon-Handheld) },
    @{ Name = "disc";      Label = "DISC";      Grid = (Icon-Disc) },
    @{ Name = "press";     Label = "PRESS";     Grid = (Icon-Press) }
)

for ($i = 0; $i -lt $icons.Count; $i++) {
    $icon = $icons[$i]
    $number = "{0:D2}" -f ($i + 1)
    $stem = "icon-$number-$($icon.Name)"
    $png = Join-Path $OutDir "$stem.png"
    $ico = Join-Path $OutDir "$stem.ico"
    $bmp = Grid-To-Bitmap $icon.Grid 256
    $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Save-Ico $icon.Grid $ico
}

# Contact sheet: 5 columns x 2 rows on the Game Boy darkest background.
$cellW = 272
$cellH = 304
$cols = 5
$rows = 2
$sheet = New-Object System.Drawing.Bitmap ($cellW * $cols), ($cellH * $rows)
$graphics = [System.Drawing.Graphics]::FromImage($sheet)
$graphics.Clear($Dark)
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
$font = New-Object System.Drawing.Font "Consolas", 13, ([System.Drawing.FontStyle]::Bold)
$brush = New-Object System.Drawing.SolidBrush $Lightest
$format = New-Object System.Drawing.StringFormat
$format.Alignment = [System.Drawing.StringAlignment]::Center
$format.LineAlignment = [System.Drawing.StringAlignment]::Center
for ($i = 0; $i -lt $icons.Count; $i++) {
    $col = $i % $cols
    $row = [math]::Floor($i / $cols)
    $x = $col * $cellW
    $y = $row * $cellH
    $iconBmp = Grid-To-Bitmap $icons[$i].Grid 256
    $graphics.DrawImage($iconBmp, $x + 8, $y + 8, 256, 256)
    $iconBmp.Dispose()
    $labelRect = New-Object System.Drawing.RectangleF $x, ($y + 268), $cellW, 28
    $label = "{0:D2}  {1}" -f ($i + 1), $icons[$i].Label
    $graphics.DrawString($label, $font, $brush, $labelRect, $format)
}
$sheet.Save((Join-Path $OutDir "icon-options.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$sheet.Dispose()
$font.Dispose()
$brush.Dispose()

Write-Host "Generated $($icons.Count) icons in $OutDir"
Get-ChildItem $OutDir -File | Sort-Object Name | Select-Object Name, Length
