# Optional: fetch a larger CC0 heightmap for Mega-W10 ChunkStream demos.
# No passwords / secrets. Review upstream license before redistributing.
#
# Usage (from repo root or this folder):
#   powershell -ExecutionPolicy Bypass -File content/scenes/large_terrain/download_large_terrain.ps1
#
# Default target: this directory. Override with -OutDir.

param(
  [string]$OutDir = $PSScriptRoot,
  # Example public-domain / CC0-style height sources (verify license on the page before use):
  # - USGS / public domain DEMs (often GeoTIFF; convert offline)
  # - Poly Haven / community CC0 height packs when available
  # Placeholder URL documents the workflow; replace with a real CC0 direct link you trust.
  [string]$Url = "https://cdn.jsdelivr.net/gh/mevedia/terrain-heightmaps@master/README.md"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host "OutDir: $OutDir"
Write-Host "This script documents how to fetch larger CC0 heightmaps."
Write-Host "It does NOT embed credentials. Prefer maps <= a few MB for learn samples;"
Write-Host "keep multi-MB originals gitignored and load via LoadHeightmapPng."
Write-Host ""
Write-Host "Suggested workflow:"
Write-Host "  1. Pick a CC0/public-domain heightmap (PNG 16-bit or 8-bit grayscale, or float raw)."
Write-Host "  2. Download with Invoke-WebRequest (example below)."
Write-Host "  3. Place as heightmap_2k.png / heightmap_4k.png next to heightmap_512.png."
Write-Host "  4. Point sample_38_large_terrain / Sandbox at the new path."
Write-Host ""

$destReadme = Join-Path $OutDir "UPSTREAM_FETCH_NOTE.txt"
@"
Fetched/attempted URL: $Url
Date: $(Get-Date -Format o)
Notes: Replace Url with a direct CC0 PNG/TIFF link. Convert GeoTIFF offline if needed.
Engine loader: engine::terrain::LoadHeightmapPng (8-bit RGBA/gray via stb_image).
"@ | Set-Content -Path $destReadme -Encoding UTF8

try {
  $probe = Join-Path $OutDir "_fetch_probe.tmp"
  Invoke-WebRequest -Uri $Url -OutFile $probe -UseBasicParsing -TimeoutSec 30
  Write-Host "Probe download OK -> $probe (safe to delete; not a heightmap)."
} catch {
  Write-Warning "Network fetch failed or URL is documentation-only: $($_.Exception.Message)"
  Write-Host "You can still use the in-repo heightmap_512.png (CC0 generated)."
}

Write-Host "Done."
