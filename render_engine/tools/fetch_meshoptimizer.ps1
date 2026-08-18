# Optional: document / clone meshoptimizer for MeshletizePreferMeshoptimizer.
# Does NOT force a download when the network is bad — clone failure is a warning only.
#
# Usage (from render_engine root):
#   powershell -ExecutionPolicy Bypass -File tools/fetch_meshoptimizer.ps1
#
# Override:
#   -DestDir  third_party/meshoptimizer
#   -RepoUrl  https://github.com/zeux/meshoptimizer.git
#   -SkipClone  (only write notes; never hit the network)

param(
  [string]$DestDir = (Join-Path $PSScriptRoot "..\third_party\meshoptimizer"),
  [string]$RepoUrl = "https://github.com/zeux/meshoptimizer.git",
  [switch]$SkipClone
)

$ErrorActionPreference = "Continue"
$DestDir = [System.IO.Path]::GetFullPath($DestDir)

Write-Host "DestDir: $DestDir"
Write-Host "RepoUrl: $RepoUrl"
Write-Host ""
Write-Host "Engine behavior without this tree:"
Write-Host "  MeshletizePreferMeshoptimizer → MeshletizeAabbGrid fallback (unit-tested)."
Write-Host "This script only documents / optionally clones; CMake does not FetchContent it."
Write-Host ""

$note = Join-Path $DestDir "FETCH_NOTE.txt"
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
@"
meshoptimizer fetch note
Date: $(Get-Date -Format o)
Repo: $RepoUrl
Dest: $DestDir
Engine: PreferMeshoptimizer falls back to AABB when sources are absent.
"@ | Set-Content -Path $note -Encoding UTF8

if ($SkipClone) {
  Write-Host "SkipClone set — wrote $note only."
  exit 0
}

$gitDir = Join-Path $DestDir ".git"
if (Test-Path $gitDir) {
  Write-Host "Already a git clone at DestDir — leaving as-is."
  exit 0
}

# If README.md stub exists but no .git, clone into a temp sibling then merge is messy;
# prefer cloning when DestDir only has stub README / FETCH_NOTE.
$hasSrc = Test-Path (Join-Path $DestDir "src")
if ($hasSrc) {
  Write-Host "src/ already present — not cloning."
  exit 0
}

Write-Host "Attempting: git clone --depth 1 $RepoUrl $DestDir"
Write-Host "(If network fails, AABB fallback remains valid; ignore and continue.)"
try {
  $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("meshoptimizer_clone_" + [guid]::NewGuid().ToString("N"))
  & git clone --depth 1 $RepoUrl $tmp
  if ($LASTEXITCODE -ne 0) {
    throw "git clone exit $LASTEXITCODE"
  }
  # Preserve our stub README if present; copy clone contents over.
  Get-ChildItem -Force $tmp | ForEach-Object {
    $target = Join-Path $DestDir $_.Name
    if ($_.Name -eq "README.md" -and (Test-Path (Join-Path $DestDir "README.md"))) {
      Copy-Item $_.FullName (Join-Path $DestDir "README.upstream.md") -Force
    } else {
      Copy-Item $_.FullName $target -Recurse -Force
    }
  }
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
  Write-Host "Clone OK."
} catch {
  Write-Warning "Clone failed (network or git missing): $($_.Exception.Message)"
  Write-Host "Left stub README + FETCH_NOTE. PreferMeshoptimizer still uses AABB."
}

Write-Host "Done."
