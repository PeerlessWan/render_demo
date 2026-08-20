# Fetch NVIDIA NGX (DLSS) + RTXGI SDK into third_party (optional).
# ADR 0046 / Mega-W23: one script prepares both drop-in trees.
# Does NOT download from the network (license). Copy from local official packages.
#
# Usage:
#   .\tools\fetch_nvidia_ngx_rtxgi.ps1
#   .\tools\fetch_nvidia_ngx_rtxgi.ps1 -NgxSource D:\sdks\ngx -RtxgiSource D:\sdks\rtxgi
#   .\tools\fetch_nvidia_ngx_rtxgi.ps1 -NgxSource D:\dlss.zip -RtxgiSource D:\RTXGI.zip

param(
  [string]$NgxOutDir = (Join-Path $PSScriptRoot "..\third_party\ngx"),
  [string]$RtxgiOutDir = (Join-Path $PSScriptRoot "..\third_party\rtxgi"),
  [string]$NgxSource = "",
  [string]$RtxgiSource = ""
)

$ErrorActionPreference = "Stop"

function Ensure-Dir([string]$path) {
  New-Item -ItemType Directory -Force -Path $path | Out-Null
}

function Copy-OrExtract([string]$source, [string]$dest) {
  if ([string]::IsNullOrWhiteSpace($source)) { return $false }
  if (-not (Test-Path $source)) {
    Write-Warning "Source not found: $source"
    return $false
  }
  Ensure-Dir $dest
  $item = Get-Item $source
  if ($item.PSIsContainer) {
    Copy-Item -Path (Join-Path $source "*") -Destination $dest -Recurse -Force
  } elseif ($item.Extension -match '\.(zip|7z)$') {
    Expand-Archive -Path $source -DestinationPath $dest -Force
  } else {
    Copy-Item -Path $source -Destination $dest -Force
  }
  return $true
}

function Write-NgxReadme([string]$dir) {
  $marker = Join-Path $dir "README_ENGINE.txt"
  @"
NVIDIA NGX / DLSS vendor drop — ADR 0046 (Mega-W23).

1. Place official NGX/DLSS SDK here so CMake finds nvsdk_ngx.h
   (include/nvsdk_ngx.h or nvsdk_ngx.h at root).
2. Runtime: nvngx_dlss.dll / nvngx.dll on PATH or next to the exe.
3. cmake: ENGINE_NGX_DIR / ENGINE_WITH_NGX (auto-ON when header present).
4. Product chain: BindUpscalerGpuDevice → CreateUpscaler → DLSS→FSR→bilinear.
5. Without evaluate/runtime → honest SKIP / builtin_bilinear.

Fetch: tools/fetch_nvidia_ngx_rtxgi.ps1 -NgxSource <official_dir_or_zip>
Do not commit proprietary .dll/.lib into git.
"@ | Set-Content -Path $marker -Encoding UTF8
}

function Write-RtxgiReadme([string]$dir) {
  $marker = Join-Path $dir "README_ENGINE.txt"
  @"
NVIDIA RTXGI (true DDGI) vendor drop — ADR 0046 (Mega-W23).

1. Place official RTXGI SDK here. CMake probes for any of:
   include/ddgi/DDGIVolume.h
   include/rtxgi/ddgi/DDGIVolume.h
   ddgi/DDGIVolume.h
   RTXGI.h / rtxgi.h
2. cmake: ENGINE_RTXGI_DIR / ENGINE_WITH_RTXGI (auto-ON when header present).
3. Product: BindGiGpuDevice → TryCreateRtxgiVolume → irradiance atlas.
4. Without SDK → SKIP; CascadeGi remains the default product GI.
5. Never rename CascadeGi as DDGI.

Fetch: tools/fetch_nvidia_ngx_rtxgi.ps1 -RtxgiSource <official_dir_or_zip>
Do not commit proprietary .dll/.lib into git.
"@ | Set-Content -Path $marker -Encoding UTF8
}

Ensure-Dir $NgxOutDir
Ensure-Dir $RtxgiOutDir
Write-NgxReadme $NgxOutDir
Write-RtxgiReadme $RtxgiOutDir

$ngxOk = Copy-OrExtract $NgxSource $NgxOutDir
$rtxOk = Copy-OrExtract $RtxgiSource $RtxgiOutDir

# Re-write READMEs after copy so they are not wiped if source lacked them.
Write-NgxReadme $NgxOutDir
Write-RtxgiReadme $RtxgiOutDir

function Test-NgxHeader([string]$dir) {
  return (Test-Path (Join-Path $dir "include\nvsdk_ngx.h")) -or
         (Test-Path (Join-Path $dir "nvsdk_ngx.h"))
}

function Test-RtxgiHeader([string]$dir) {
  $candidates = @(
    "include\ddgi\DDGIVolume.h",
    "include\rtxgi\ddgi\DDGIVolume.h",
    "ddgi\DDGIVolume.h",
    "include\RTXGI.h",
    "RTXGI.h",
    "include\rtxgi.h",
    "rtxgi.h"
  )
  foreach ($c in $candidates) {
    if (Test-Path (Join-Path $dir $c)) { return $true }
  }
  return $false
}

Write-Host "NGX out:    $NgxOutDir  header=$(Test-NgxHeader $NgxOutDir)  copied=$ngxOk"
Write-Host "RTXGI out:  $RtxgiOutDir  header=$(Test-RtxgiHeader $RtxgiOutDir)  copied=$rtxOk"
if (-not $ngxOk -or -not $rtxOk) {
  Write-Host "Scaffold ready. Obtain official SDKs from NVIDIA Developer, then re-run with -NgxSource / -RtxgiSource."
}
Write-Host "Done (ADR 0046). Proprietary binaries must stay local — do not git-add .dll/.lib."
