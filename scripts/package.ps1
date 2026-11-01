# builds, tests and packages the workbench into <CandidateId>_Workbench.zip.
# packaging only: no backend feature lives here.
#
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -CandidateId ABC123
#
# needs cmake, ninja and a c++17 compiler on PATH (see DEPENDENCIES.md).

param(
    [string]$CandidateId = "CandidateID",
    [switch]$RegenerateResults
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

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
# fresh test output for this exact build, written into the package only
& .\build\wb_tests.exe | Out-File -Encoding utf8 "$dist\docs\test_output.txt"

Step "version-control evidence"
git bundle create "$dist\repo\workbench.bundle" --all
git log --stat --date=iso --format="commit %h  %ad%nauthor %an%n%n    %s%n%n%b" |
    Out-File -Encoding utf8 "$dist\repo\commit_log.txt"
git log --oneline | Out-File -Encoding utf8 "$dist\repo\commit_log_oneline.txt"

Step "zip"
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $dist -DestinationPath $zip
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "`nwrote $zip ($mb MB)" -ForegroundColor Green
