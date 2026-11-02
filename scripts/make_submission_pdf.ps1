# builds the single pdf that the submission form accepts (one pdf, <= 1 mb):
# a cover page that links to the full zip (with its sha-256) followed by the report.
# packaging only: it reads files and prints html to pdf with microsoft edge.
#
#   powershell -ExecutionPolicy Bypass -File scripts\make_submission_pdf.ps1 -CandidateId ABC123 -ZipUrl https://... [-RepoUrl https://...] [-CandidateName "..."] [-RegisterNo ...]
#
# run scripts\package.ps1 first so dist\<id>_Workbench.zip exists.

param(
    [Parameter(Mandatory = $true)][string]$CandidateId,
    [Parameter(Mandatory = $true)][string]$ZipUrl,
    [string]$RepoUrl = "",
    [string]$CandidateName = "",
    [string]$RegisterNo = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$zip = Join-Path $root "dist\${CandidateId}_Workbench.zip"
if (-not (Test-Path $zip)) { throw "missing $zip. run scripts\package.ps1 -CandidateId $CandidateId first" }
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw "microsoft edge was not found; it is used to print html to pdf" }

$handwritten = Join-Path $root "docs\handwritten_backprop.pdf"
$hasHandwritten = Test-Path $handwritten
$appendixNote = if ($hasHandwritten) { "handwritten pages: appended at the end of this pdf, and docs/handwritten_backprop.pdf in the zip" }
                else { "handwritten pages: not included yet (docs/handwritten_backprop.pdf)" }

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
$commit = (git -C $root rev-parse --short HEAD).Trim()
$date = Get-Date -Format "yyyy-MM-dd"
$esc = [System.Net.WebUtility]::HtmlEncode($ZipUrl)
$enc = { param($t) [System.Net.WebUtility]::HtmlEncode($t) }
$idRows = ""
if ($CandidateName) { $idRows += "<tr><td>name</td><td>$(& $enc $CandidateName)</td></tr>" }
if ($RegisterNo) { $idRows += "<tr><td>register no.</td><td>$(& $enc $RegisterNo)</td></tr>" }
$repoRow = if ($RepoUrl) { $r = & $enc $RepoUrl; "<tr><td>repository</td><td><a href=`"$r`">$r</a><br><span class=`"note`">full commit history with the original timestamps</span></td></tr>" } else { "" }
$appendixPages = if ($hasHandwritten) { "; handwritten appendix, last two pages" } else { "" }

$cover = @"
<section class="cover">
  <div class="kicker">pass science works &middot; r&amp;d internship 2026&ndash;27 &middot; common technical assignment</div>
  <h1 class="title">unified engineering workbench</h1>
  <div class="subtitle">submission cover and technical report</div>
  <table class="ident">
    $idRows
    <tr><td>candidate id</td><td>$CandidateId</td></tr>
    <tr><td>submitted</td><td>$date</td></tr>
    <tr><td>git commit</td><td><code>$commit</code></td></tr>
  </table>

  <h2>submission</h2>
  <p>the full submission is one zip: Launch.exe and the packaged app, source, tests, data, results, docs and the git bundle.
  the form takes a single pdf of at most 1 mb, so this document carries the report and points to the zip.</p>
  <table class="links">
    <tr><td>zip</td><td><a href="$esc">$esc</a></td></tr>
    $repoRow
    <tr><td>file</td><td>${CandidateId}_Workbench.zip, $mb MB</td></tr>
    <tr><td>sha-256</td><td><code>$hash</code></td></tr>
  </table>

  <h2>how to run</h2>
  <p>extract the zip and double-click <code>Launch.exe</code>. the default browser opens the app on 127.0.0.1. no install,
  admin rights or internet are needed. in vs code: open the folder, run and debug, &ldquo;run workbench (packaged
  Launch.exe)&rdquo;. building from source is described in README.md.</p>

  <h2>where things are</h2>
  <table>
    <tr><th>brief</th><th>evidence in the zip</th></tr>
    <tr><td>a &nbsp;interface, one-click</td><td>Launch.exe, app/www, .vscode/, README.md; the in-app overview maps every bullet of the brief</td></tr>
    <tr><td>b &nbsp;cad / ansys data</td><td>src/core/stl.*, nodal_csv.*; data/examples, data/PROVENANCE.md; results/file_size_tests.csv</td></tr>
    <tr><td>c &nbsp;thermal fea</td><td>src/core/fea1d.*, thermal_study.*; results/fea_*.csv and .svg; report &sect;2</td></tr>
    <tr><td>d &nbsp;multicore</td><td>src/core/worker_pool.*, batch.*; results/benchmark_session1&ndash;3, launch_to_ready.csv; report &sect;3</td></tr>
    <tr><td>e &nbsp;embedded</td><td>src/core/hardware.*; docs/embedded_design.md (block diagram, pin/voltage table, plan)</td></tr>
    <tr><td>f &nbsp;backprop</td><td>src/core/backprop.*; results/backprop_*; $appendixNote</td></tr>
    <tr><td>tests, vcs, ai use</td><td>tests/ (78 tests), docs/test_output.txt, repo/workbench.bundle and commit_log.txt, docs/ai_use_log.md, docs/ownership.md</td></tr>
    <tr><td>demo</td><td>docs/walkthrough/ (14 timestamped frames captured from the running app, with the backend log)</td></tr>
  </table>
  <p class="contents">contents: this cover; the report, five pages$appendixPages.</p>
</section>
"@

$report = Get-Content (Join-Path $root "docs\report.html") -Raw -Encoding UTF8
$report = $report.Replace('candidate id: ________', "candidate id: $CandidateId")
$report = $report.Replace('<body>', "<body>`n$cover`n<div style=`"break-before: page`"></div>")
$coverCss = @"
  .kicker { font-variant: small-caps; letter-spacing: 0.08em; font-size: 9pt; padding-bottom: 5pt; border-bottom: 0.75pt solid #000; }
  .title { font-size: 30pt; font-weight: 400; letter-spacing: -0.015em; margin: 26pt 0 2pt; }
  .subtitle { font-style: italic; font-size: 11pt; margin-bottom: 16pt; }
  table.ident, table.links { border: 0; width: auto; font-size: 9.8pt; margin: 0 0 8pt; }
  table.ident td, table.links td { padding: 1.5pt 14pt 1.5pt 0; }
  table.ident td:first-child, table.links td:first-child { font-style: italic; white-space: nowrap; }
  table.links { border-top: 1pt solid #000; border-bottom: 1pt solid #000; width: 100%; }
  table.links td { padding: 3pt 10pt 3pt 0; }
  table.links code { font-size: 7.8pt; }
  .note { font-size: 8pt; font-style: italic; }
  .contents { font-style: italic; font-size: 8.5pt; margin-top: 8pt; }
"@
$report = $report.Replace('</style>', "$coverCss</style>")
$tmp = Join-Path $root "docs\_submission.html"
[System.IO.File]::WriteAllText($tmp, $report, (New-Object System.Text.UTF8Encoding($false)))

$out = Join-Path $root "dist\${CandidateId}_Workbench_submission.pdf"
$uri = "file:///" + ($tmp -replace '\\', '/')
$p = Start-Process $edge -ArgumentList @('--headless=new', '--disable-gpu', '--no-pdf-header-footer', "--print-to-pdf=$out", $uri) -PassThru
if (-not $p.WaitForExit(90000)) { Stop-Process -Id $p.Id -Force; throw "edge did not finish printing" }
Remove-Item $tmp

if ($hasHandwritten) {
    # the form takes a single pdf, so the handwritten appendix is appended to it (needs python + pypdf)
    $merge = @'
import sys
from pypdf import PdfReader, PdfWriter
out, extra = sys.argv[1], sys.argv[2]
w = PdfWriter()
for path in (out, extra):
    w.append(PdfReader(path))
n = len(PdfReader(extra).pages)
with open(out, "wb") as f:
    w.write(f)
print(n)
'@
    $pages = $merge | python -I - $out $handwritten
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "could not append the handwritten pages (needs python 3 with pypdf: pip install pypdf). the pdf has the report only."
    } else {
        Write-Host "appended the handwritten appendix ($pages page(s))"
        if ([int]$pages -gt 2) { Write-Warning "the handwritten appendix has $pages pages; the brief allows at most two" }
    }
} else {
    Write-Warning "docs\handwritten_backprop.pdf not found: the pdf has no handwritten appendix (see scripts\handwritten_to_pdf.ps1)"
}

$kb = [math]::Round((Get-Item $out).Length / 1KB)
Write-Host "wrote $out ($kb KB)" -ForegroundColor Green
if ($kb -gt 1000) { Write-Warning "the pdf is over 1 MB; the form may reject it" }
