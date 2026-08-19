# Optional: vendor meshoptimizer into third_party/meshoptimizer for product meshlet cook.
# Without it, MeshletizePreferMeshoptimizer falls back to AABB (honest).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/fetch_meshoptimizer.ps1

param(
  [string]$OutDir = (Join-Path $PSScriptRoot "..\third_party\meshoptimizer")
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$marker = Join-Path $OutDir "README_ENGINE.txt"
@"
meshoptimizer drop-in for ENGINE meshlet cook (W13 / ADR 0039).

Clone or copy meshoptimizer sources here, then reconfigure CMake.
Until present, GPU-driven meshlet path uses AABB fallback.
See engine/gpu_driven/meshlet.cpp and docs/KNOWN_GAPS.md.
"@ | Set-Content -Path $marker -Encoding UTF8
Write-Host "Wrote $marker"
