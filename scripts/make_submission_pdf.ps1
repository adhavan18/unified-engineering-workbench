# builds the single pdf that the submission form accepts (one pdf, <= 1 mb):
# a cover page that links to the full zip (with its sha-256) followed by the report.
# packaging only: it reads files and prints html to pdf with microsoft edge.
#
#   powershell -ExecutionPolicy Bypass -File scripts\make_submission_pdf.ps1 -CandidateId ABC123 -ZipUrl https://...
#
# run scripts\package.ps1 first so dist\<id>_Workbench.zip exists.

param(
    [Parameter(Mandatory = $true)][string]$CandidateId,
    [Parameter(Mandatory = $true)][string]$ZipUrl
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$zip = Join-Path $root "dist\${CandidateId}_Workbench.zip"
if (-not (Test-Path $zip)) { throw "missing $zip. run scripts\package.ps1 -CandidateId $CandidateId first" }
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw "microsoft edge was not found; it is used to print html to pdf" }

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
$commit = (git -C $root rev-parse --short HEAD).Trim()
$date = Get-Date -Format "yyyy-MM-dd"
$esc = [System.Net.WebUtility]::HtmlEncode($ZipUrl)

$cover = @"
<section class="cover">
  <div class="eyebrow">pass science works · r&amp;d internship 2026–27 · common technical assignment</div>
  <h1 style="font-size:24pt;margin:6pt 0 4pt">unified engineering workbench</h1>
  <p class="sub">candidate id: <b>$CandidateId</b> · submitted $date · git commit $commit</p>
  <div class="box" style="margin:14pt 0">
    <p style="margin:0 0 6pt"><b>the full submission is a zip</b> (Launch.exe, source, tests, data, results, docs, git bundle).
    the form accepts one pdf of at most 1 mb, so this pdf carries the report and a link to the zip.</p>
    <p style="margin:0 0 4pt">download: <a href="$esc">$esc</a></p>
    <p style="margin:0 0 4pt">file: ${CandidateId}_Workbench.zip · $mb MB</p>
    <p style="margin:0">sha-256: <code style="font-size:8pt">$hash</code></p>
  </div>
  <h2>how to run</h2>
  <p>extract the zip, double-click <code>Launch.exe</code>. the default browser opens the app on 127.0.0.1; no install, admin rights or
  internet. vs code: open the folder, run and debug → "run workbench (packaged Launch.exe)". source build: README.md.</p>
  <h2>what is where in the zip</h2>
  <table>
    <tr><th>brief section</th><th>evidence</th></tr>
    <tr><td>a · interface, one-click</td><td>Launch.exe, app/www, .vscode/, README.md; the in-app overview maps every bullet of the brief</td></tr>
    <tr><td>b · cad / ansys data</td><td>src/core/stl.*, nodal_csv.*; data/examples + data/PROVENANCE.md; results/file_size_tests.csv</td></tr>
    <tr><td>c · thermal fea</td><td>src/core/fea1d.*, thermal_study.*; results/fea_*.csv + .svg; report §2</td></tr>
    <tr><td>d · multicore</td><td>src/core/worker_pool.*, batch.*; results/benchmark_session1-3, launch_to_ready.csv; report §3</td></tr>
    <tr><td>e · embedded</td><td>src/core/hardware.*; docs/embedded_design.md (block diagram, pin/voltage table, plan)</td></tr>
    <tr><td>f · backprop</td><td>src/core/backprop.*; results/backprop_*; handwritten pages: docs/handwritten_backprop.pdf</td></tr>
    <tr><td>tests, vcs, ai use</td><td>tests/ (77 tests), docs/test_output.txt, repo/workbench.bundle + commit_log.txt, docs/ai_use_log.md, docs/ownership.md</td></tr>
    <tr><td>demo</td><td>docs/walkthrough/ (timestamped screenshots + backend log) and the demo video, if submitted separately</td></tr>
  </table>
  <p class="sub">the report follows (5 pages).</p>
</section>
"@

$report = Get-Content (Join-Path $root "docs\report.html") -Raw -Encoding UTF8
$report = $report.Replace('candidate id: ________', "candidate id: $CandidateId")
$report = $report.Replace('<body>', "<body>`n$cover`n<div style=`"break-before: page`"></div>")
$report = $report.Replace('</style>', "  .cover h2 { margin-top: 12pt; }`n  .eyebrow { font-size: 8pt; font-weight: 700; letter-spacing: .08em; text-transform: uppercase; color: var(--accent); }`n</style>")
$tmp = Join-Path $root "docs\_submission.html"
[System.IO.File]::WriteAllText($tmp, $report, (New-Object System.Text.UTF8Encoding($false)))

$out = Join-Path $root "dist\${CandidateId}_Workbench_submission.pdf"
$uri = "file:///" + ($tmp -replace '\\', '/')
$p = Start-Process $edge -ArgumentList @('--headless=new', '--disable-gpu', '--no-pdf-header-footer', "--print-to-pdf=$out", $uri) -PassThru
if (-not $p.WaitForExit(90000)) { Stop-Process -Id $p.Id -Force; throw "edge did not finish printing" }
Remove-Item $tmp
$kb = [math]::Round((Get-Item $out).Length / 1KB)
Write-Host "wrote $out ($kb KB)" -ForegroundColor Green
if ($kb -gt 1000) { Write-Warning "the pdf is over 1 MB; the form may reject it" }
