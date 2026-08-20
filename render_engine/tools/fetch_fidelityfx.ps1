# Fetch AMD FidelityFX FSR2 headers/libs into third_party (optional).
# ADR 0044: prepares drop-in directory. Does NOT download SDKs.
# After placing official FSR2 SDK, reconfigure with -DENGINE_WITH_FIDELITYFX=ON.
# TryCreateFsr2Upscaler returns a live upscaler only when Upscale() dispatches into FFX.

param(
  [string]$OutDir = (Join-Path $PSScriptRoot "..\third_party\fidelityfx")
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$marker = Join-Path $OutDir "README_ENGINE.txt"
@"
FidelityFX / FSR2 vendor drop (manual) — ADR 0044 unfreeze.

1. Place official FSR2 SDK headers/libs here (ffx_fsr2.h).
2. cmake -DENGINE_WITH_FIDELITYFX=ON (or ENGINE_WITH_FFX=ON).
3. GPU FFX context must be bound before name()=fsr2 is returned from CreateUpscaler.
4. Until then TryCreateFsr2Upscaler() returns nullptr → builtin_bilinear (honest).

See docs/learn/adr/0044-w21-godot-parity-unfreeze.md and ADR 0008.
"@ | Set-Content -Path $marker -Encoding UTF8
Write-Host "Wrote $marker (manual SDK drop-in). No automatic download (license/size)."
