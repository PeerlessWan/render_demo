# Optional: refresh / replace the large-terrain heightmap (Mega-W10 / ADR 0037).
# No passwords / secrets.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File content/scenes/large_terrain/download_large_terrain.ps1
#
# Default: regenerates in-repo CC0 procedural heightmap_512.png (no network required).
# Pass -FetchUrl to try downloading an alternate grayscale PNG (verify license yourself).

param(
  [string]$OutDir = $PSScriptRoot,
  [string]$FetchUrl = ""
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outPng = Join-Path $OutDir "heightmap_512.png"

if ($FetchUrl -ne "") {
  Write-Host "Fetching $FetchUrl ..."
  $tmp = Join-Path $OutDir "_heightmap_fetch.tmp"
  Invoke-WebRequest -Uri $FetchUrl -OutFile $tmp -UseBasicParsing -TimeoutSec 60
  Move-Item -Force $tmp $outPng
  Write-Host "Wrote $outPng"
  exit 0
}

# Procedural CC0 generator (same intent as README): multi-peak grayscale PNG.
$py = @"
import math, struct, zlib
from pathlib import Path
W=H=512
peaks=[(0.28,0.32,0.55,0.12),(0.62,0.40,0.70,0.15),(0.45,0.68,0.48,0.18),(0.18,0.72,0.35,0.10),(0.78,0.75,0.42,0.11),(0.52,0.22,0.30,0.20)]
rows=[]
for y in range(H):
  row=[]
  for x in range(W):
    u=x/(W-1); v=y/(H-1)
    h=0.08
    for cx,cy,amp,sig in peaks:
      d2=(u-cx)**2+(v-cy)**2
      h+=amp*math.exp(-d2/(2*sig*sig))
    h+=0.12*math.sin(u*9.0)*math.sin(v*7.0+0.4)
    edge=min(u,v,1-u,1-v)
    h*=min(1.0, edge*6.0)
    h=max(0.0,min(1.0,h))
    g=int(h*255+0.5)
    row.append(bytes((g,g,g,255)))
  rows.append(b''.join(row))
def chunk(tag, data):
  return struct.pack('>I', len(data))+tag+data+struct.pack('>I', zlib.crc32(tag+data)&0xffffffff)
raw=b''.join(b'\x00'+r for r in rows)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', W,H,8,6,0,0,0))+chunk(b'IDAT', zlib.compress(raw,9))+chunk(b'IEND', b'')
Path(r'$($outPng -replace '\\','/')').write_bytes(png)
print('wrote', Path(r'$($outPng -replace '\\','/')'), len(png))
"@
$tmpPy = Join-Path $OutDir "_gen_hm.py"
Set-Content -Path $tmpPy -Value $py -Encoding UTF8
python $tmpPy
Remove-Item -Force $tmpPy
Write-Host "Done (CC0 procedural heightmap_512.png)."
