# turns photos or scans of the handwritten backprop pages into docs\handwritten_backprop.pdf.
# documentation tooling only: it resizes images and prints them to pdf with microsoft edge.
#
#   1. put the photos in docs\handwritten\ (jpg or png, at most two; they are used in name order,
#      so call them page1.jpg and page2.jpg)
#   2. powershell -ExecutionPolicy Bypass -File scripts\handwritten_to_pdf.ps1
#
# images are scaled to at most 1800 px on the long side (about 200 dpi on a4) and saved as jpeg at
# <= 300 kb per page, which keeps them legible and lets them fit next to the report in the 1 mb
# submission pdf.

param(
    [string]$InputDir = "",
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
if (-not $InputDir) { $InputDir = Join-Path $root "docs\handwritten" }
if (-not $OutFile) { $OutFile = Join-Path $root "docs\handwritten_backprop.pdf" }

$images = @(Get-ChildItem $InputDir -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -match '^\.(jpe?g|png)$' } | Sort-Object Name)
if ($images.Count -eq 0) { throw "no .jpg or .png pages found in $InputDir" }
if ($images.Count -gt 2) { throw "found $($images.Count) images in $InputDir; the brief allows at most two pages" }

$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw "microsoft edge was not found; it is used to print the pages to pdf" }

$work = Join-Path $env:TEMP ("wb-handwritten-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $work | Out-Null
$jpeg = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$quality = New-Object System.Drawing.Imaging.EncoderParameters(1)

$pages = @()
$i = 0
foreach ($img in $images) {
    $i++
    $src = [System.Drawing.Image]::FromFile($img.FullName)
    try {
        # phone photos often store rotation in exif (tag 0x0112) instead of rotating the pixels
        if ($src.PropertyIdList -contains 0x0112) {
            $o = $src.GetPropertyItem(0x0112).Value[0]
            $flip = @{ 3 = "Rotate180FlipNone"; 6 = "Rotate90FlipNone"; 8 = "Rotate270FlipNone" }[[int]$o]
            if ($flip) { $src.RotateFlip([System.Drawing.RotateFlipType]::$flip) }
        }
        $scale = [math]::Min(1.0, 1800.0 / [math]::Max($src.Width, $src.Height))
        $w = [int]($src.Width * $scale); $h = [int]($src.Height * $scale)
        $bmp = New-Object System.Drawing.Bitmap($w, $h)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.Clear([System.Drawing.Color]::White)
        $g.DrawImage($src, 0, 0, $w, $h)
        $g.Dispose()
        $name = "page$i.jpg"
        $file = Join-Path $work $name
        # noisy phone photos compress badly: step the quality down until a page is <= 300 kb,
        # so two pages plus the report stay under the form's 1 mb limit
        foreach ($q in 80, 70, 60, 50, 40) {
            $quality.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]$q)
            $bmp.Save($file, $jpeg, $quality)
            if ((Get-Item $file).Length -le 300KB) { break }
        }
        $bmp.Dispose()
        $pages += $name
        Write-Host ("page ${i}: $($img.Name) -> ${w} x ${h} px, jpeg quality $q, {0} KB" -f [math]::Round((Get-Item $file).Length / 1KB))
    } finally { $src.Dispose() }
}

$body = ($pages | ForEach-Object { "<div class=`"page`"><img src=`"$_`"></div>" }) -join "`n"
$html = @"
<!doctype html><html><head><meta charset="utf-8"><title>handwritten backpropagation</title><style>
@page { size: A4; margin: 8mm; }
body { margin: 0; }
.page { height: 279mm; display: flex; align-items: center; justify-content: center; break-after: page; }
.page:last-child { break-after: auto; }
img { max-width: 100%; max-height: 100%; object-fit: contain; }
</style></head><body>
$body
</body></html>
"@
$htmlPath = Join-Path $work "pages.html"
[System.IO.File]::WriteAllText($htmlPath, $html, (New-Object System.Text.UTF8Encoding($false)))

$uri = "file:///" + ($htmlPath -replace '\\', '/')
$p = Start-Process $edge -ArgumentList @('--headless=new', '--disable-gpu', '--no-pdf-header-footer', "--print-to-pdf=$OutFile", $uri) -PassThru
if (-not $p.WaitForExit(90000)) { Stop-Process -Id $p.Id -Force; throw "edge did not finish printing" }
Remove-Item -Recurse -Force $work
$kb = [math]::Round((Get-Item $OutFile).Length / 1KB)
Write-Host "wrote $OutFile ($($pages.Count) page(s), $kb KB)" -ForegroundColor Green
