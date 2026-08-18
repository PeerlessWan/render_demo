# Fetch Kenney Blocky Characters (CC0) and copy one small GLB into this folder.
# No passwords / secrets. Full pack ~2.1 MB; committed demo uses a single ~110 KB GLB.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File content/characters/download_characters.ps1

param(
  [string]$OutDir = $PSScriptRoot,
  [string]$Url = "https://opengameart.org/sites/default/files/kenney_blocky-characters_2.0.zip",
  [string]$PreferredGlb = "character-a.glb",
  [string]$OutName = "kenney_blocky_character.glb"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$zipPath = Join-Path $OutDir "_kenney_blocky-characters.zip"
$extract = Join-Path $OutDir "_extract_kenney"

Write-Host "OutDir: $OutDir"
Write-Host "Fetching: $Url"

try {
  Invoke-WebRequest -Uri $Url -OutFile $zipPath -UseBasicParsing -TimeoutSec 120
  Write-Host "Downloaded $((Get-Item $zipPath).Length) bytes"
} catch {
  Write-Warning "Network fetch failed: $($_.Exception.Message)"
  Write-Host "Keeping existing $OutName if present. See LICENSE.txt."
  exit 1
}

if (Test-Path $extract) {
  Remove-Item -Recurse -Force $extract
}
Expand-Archive -Path $zipPath -DestinationPath $extract -Force

$src = Get-ChildItem -Path $extract -Recurse -Filter $PreferredGlb | Select-Object -First 1
if (-not $src) {
  $src = Get-ChildItem -Path $extract -Recurse -Filter "*.glb" | Sort-Object Length | Select-Object -First 1
}
if (-not $src) {
  Write-Error "No .glb found inside zip"
  exit 1
}

$dest = Join-Path $OutDir $OutName
Copy-Item -Force $src.FullName $dest
Write-Host "Wrote $dest ($((Get-Item $dest).Length) bytes) from $($src.Name)"

Remove-Item -Recurse -Force $extract
Remove-Item -Force $zipPath
Write-Host "Done (CC0 Kenney Blocky Characters)."
