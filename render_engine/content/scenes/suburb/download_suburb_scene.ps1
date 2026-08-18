# Fetch Kenney City Kit Suburban (CC0) and refresh curated models/ for Sandbox.
# Usage:
#   powershell -ExecutionPolicy Bypass -File content/scenes/suburb/download_suburb_scene.ps1

param(
  [string]$OutDir = $PSScriptRoot,
  [string]$Url = "https://opengameart.org/sites/default/files/kenney_city-kit-suburban_2.0.zip"
)

$ErrorActionPreference = "Stop"
$models = Join-Path $OutDir "models"
New-Item -ItemType Directory -Force -Path $models | Out-Null
$zip = Join-Path $OutDir "_kenney_city-kit-suburban.zip"
$extract = Join-Path $OutDir "_extract"

Write-Host "Fetching $Url"
Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing -TimeoutSec 180
if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
Expand-Archive -Path $zip -DestinationPath $extract -Force
$glbRoot = Get-ChildItem $extract -Recurse -Directory -Filter "GLB format" | Select-Object -First 1
if (-not $glbRoot) { Write-Error "GLB format folder missing"; exit 1 }
$keep = @(
  "building-type-a.glb","building-type-b.glb","building-type-c.glb","building-type-d.glb",
  "building-type-e.glb","building-type-h.glb","building-type-l.glb","building-type-t.glb",
  "tree-large.glb","tree-small.glb","fence.glb","path-long.glb","driveway-short.glb","planter.glb"
)
foreach ($n in $keep) {
  Copy-Item -Force (Join-Path $glbRoot.FullName $n) (Join-Path $models $n)
}
$tex = Join-Path $glbRoot.FullName "Textures"
if (Test-Path $tex) {
  Copy-Item -Recurse -Force $tex (Join-Path $models "Textures")
}
Remove-Item -Recurse -Force $extract
Remove-Item -Force $zip
Write-Host "Done -> $models"
