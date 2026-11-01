# builds, tests and packages the workbench into <CandidateId>_Workbench.zip.
# packaging only: no backend feature lives here.
#
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -CandidateId ABC123
#
# needs cmake, ninja and a c++17 compiler on PATH (see DEPENDENCIES.md), and microsoft edge to
# print the handwritten pages and the report with the candidate id. it ends with a readiness check
# listing anything the candidate still has to add.

param(
    [string]$CandidateId = "CandidateID",
    [switch]$RegenerateResults
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1

Step "configure + build (release)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Step "tests"
ctest --test-dir build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "tests failed; not packaging" }

if ($RegenerateResults) {
    Step "regenerate results/"
    & .\build\wb_report.exe all results 400 20000
}

# photos of the handwritten pages in docs\handwritten\ become docs\handwritten_backprop.pdf
$photos = @(Get-ChildItem docs\handwritten -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -match '^\.(jpe?g|png)$' })
if ($photos.Count -gt 0 -and -not (Test-Path docs\handwritten_backprop.pdf)) {
    Step "handwritten pages -> docs\handwritten_backprop.pdf"
    & (Join-Path $PSScriptRoot "handwritten_to_pdf.ps1")
}

$name = "${CandidateId}_Workbench"
$dist = Join-Path $root "dist\$name"
Step "assemble $dist"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force "$dist\app\www", "$dist\repo" | Out-Null

Copy-Item build\Launch.exe $dist
Copy-Item -Recurse src\frontend\* "$dist\app\www"
foreach ($d in "src", "tests", "tools", "data", "docs", "results", "scripts", ".vscode") {
    Copy-Item -Recurse $d (Join-Path $dist $d)
}
foreach ($f in "CMakeLists.txt", "README.md", "DEPENDENCIES.md", "toolchain.lock", ".gitignore", ".gitattributes") {
    Copy-Item $f $dist
}
# the raw phone photos stay in the git history; the package ships the 2-page pdf made from them
if (Test-Path "$dist\docs\handwritten_backprop.pdf") { Remove-Item -Recurse -Force "$dist\docs\handwritten" -ErrorAction SilentlyContinue }
# fresh test output for this exact build, written into the package only
& .\build\wb_tests.exe | Out-File -Encoding utf8 "$dist\docs\test_output.txt"

# the packaged report carries the candidate id (the repo copy keeps the blank)
if ($CandidateId -ne "CandidateID" -and $edge) {
    $html = Get-Content docs\report.html -Raw -Encoding UTF8
    $html = $html.Replace('candidate id: ________', "candidate id: $CandidateId")
    [System.IO.File]::WriteAllText("$dist\docs\report.html", $html, (New-Object System.Text.UTF8Encoding($false)))
    $uri = "file:///" + ("$dist\docs\report.html" -replace '\\', '/')
    $p = Start-Process $edge -ArgumentList @('--headless=new', '--disable-gpu', '--no-pdf-header-footer', "--print-to-pdf=$dist\docs\report.pdf", $uri) -PassThru
    if (-not $p.WaitForExit(90000)) { Stop-Process -Id $p.Id -Force; throw "edge did not finish printing the report" }
}

Step "version-control evidence"
git bundle create "$dist\repo\workbench.bundle" --all
git log --stat --date=iso --format="commit %h  %ad%nauthor %an%n%n    %s%n%n%b" |
    Out-File -Encoding utf8 "$dist\repo\commit_log.txt"
git log --oneline | Out-File -Encoding utf8 "$dist\repo\commit_log_oneline.txt"

Step "zip"
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
# windows' bundled bsdtar writes standard zip entries with '/' separators;
# Compress-Archive in windows powershell 5.1 writes '\', which breaks unzip on other systems
& "$env:SystemRoot\System32\tar.exe" -a -c -f $zip -C (Join-Path $root "dist") $name
if ($LASTEXITCODE -ne 0) { throw "zip failed" }
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "`nwrote $zip ($mb MB)" -ForegroundColor Green

# readiness: the parts of the submission only the candidate can provide
Step "readiness check"
$todo = @()
if ($CandidateId -eq "CandidateID") { $todo += "candidate id: rerun with -CandidateId <your id from the pass email>" }
if (-not (Test-Path docs\handwritten_backprop.pdf)) { $todo += "handwritten pages: put two photos in docs\handwritten\ (page1.jpg, page2.jpg) and rerun" }
if ((Get-Content docs\effort_log.md -Raw) -match '___ h') { $todo += "docs\effort_log.md: fill in your hours and the total" }
if ((Get-Content docs\ownership.md -Raw) -match 'signed: _{4,}') { $todo += "docs\ownership.md: adjust if needed, then sign and date" }
if ((Get-Content docs\ai_use_log.md -Raw) -match 'to be completed by the candidate') { $todo += "docs\ai_use_log.md: write the 'candidate's own edits and review' section" }
if (git status --porcelain) { $todo += "uncommitted changes: commit them, then rerun so the git bundle matches the package" }
if ($todo.Count -eq 0) {
    Write-Host "everything the brief asks for is in place" -ForegroundColor Green
} else {
    foreach ($t in $todo) { Write-Host "  [ ] $t" -ForegroundColor Yellow }
    Write-Host "after fixing these, rerun this script, then scripts\make_submission_pdf.ps1" -ForegroundColor Yellow
}
