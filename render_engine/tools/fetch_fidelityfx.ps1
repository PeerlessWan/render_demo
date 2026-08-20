# Fetch AMD FidelityFX FSR2 headers/libs into third_party (optional).
# W16 ADR 0040: this script only prepares a drop-in directory — it does NOT download
# SDKs and does NOT enable fake name=fsr2 Upscalers. CreateUpscaler stays on
# builtin_bilinear until real FFX Upscale() is wired under ENGINE_WITH_FIDELITYFX.
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
FidelityFX / FSR2 vendor drop (manual).

1. Place official FSR2 SDK headers/libs here.
2. Reconfigure CMake with -DENGINE_WITH_FIDELITYFX=ON only after Upscale()
   dispatches into FFX (see engine/media/upscaler_backends.cpp).
3. Until dispatch is wired, TryCreateFsr2Upscaler() returns nullptr (ADR 0040).

Do not claim name()=fsr2 while calling bilinear.
See docs/learn/adr/0008-upscaler-fallback.md and ADR 0040.
"@ | Set-Content -Path $marker -Encoding UTF8
Write-Host "Wrote $marker (manual SDK drop-in). No automatic download (license/size)."
