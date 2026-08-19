# Fetch AMD FidelityFX FSR2 headers/libs into third_party (optional).
# When present, CMake may set ENGINE_WITH_FIDELITYFX=1 for real Fsr2Upscaler.
# Without SDK, CreateUpscaler falls back to builtin_bilinear (ADR 0008 / 0039).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/fetch_fidelityfx.ps1

param(
  [string]$OutDir = (Join-Path $PSScriptRoot "..\third_party\fidelityfx")
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$marker = Join-Path $OutDir "README_ENGINE.txt"
@"
FidelityFX / FSR2 vendor drop for ENGINE_WITH_FIDELITYFX.

Place SDK headers and libs here, then reconfigure CMake with:
  -DENGINE_WITH_FIDELITYFX=ON

Until then, CreateUpscaler() uses builtin_bilinear and logs when ENGINE_UPSCALER=fsr.
See docs/learn/adr/0008-upscaler-fallback.md and ADR 0039.
"@ | Set-Content -Path $marker -Encoding UTF8
Write-Host "Wrote $marker (manual SDK drop-in). No automatic download (license/size)."
