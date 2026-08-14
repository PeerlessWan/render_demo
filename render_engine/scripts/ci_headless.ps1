# CI helper: CPU headless unit tests + optional GPU offscreen Sandbox + golden.
param(
  [string]$Config = "Debug",
  [string]$BuildDir = "",
  [switch]$Golden,
  [switch]$Validation
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
  $BuildDir = Join-Path $root "build"
}

if ($Validation) {
  $env:ENGINE_ENABLE_VALIDATION = "1"
  Write-Host "== Validation requested (ENGINE_ENABLE_VALIDATION=1) =="
}

$unit = Join-Path $BuildDir "tests\$Config\engine_unit_tests.exe"
$sandbox = Join-Path $BuildDir "samples\Sandbox\$Config\sample_sandbox.exe"

if (-not (Test-Path $unit)) {
  Write-Error "Missing $unit — build engine_unit_tests first"
}

Write-Host "== engine_unit_tests =="
& $unit
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

if (-not (Test-Path $sandbox)) {
  Write-Host "[SKIP] sample_sandbox missing — skip gpu-headless"
  exit 0
}

Write-Host "== sample_sandbox --gpu-headless --backend=d3d12 =="
& $sandbox --gpu-headless --headless_frames=4 --backend=d3d12
$code = $LASTEXITCODE
if ($code -ne 0) {
  Write-Host "[FAIL] gpu-headless exit $code"
  exit $code
}
Write-Host "[PASS] gpu-headless"

# Windowed path (shown HWND, auto-close): exercises scale instancing inside OpaqueLit.
# Use enough frames to cover reflection probe (%8) + multi-buffer instance uploads.
Write-Host "== sample_sandbox windowed scale-path smoke (--headless_frames=90, no --gpu-headless) =="
& $sandbox --backend=d3d12 --headless_frames=90
$code = $LASTEXITCODE
if ($code -ne 0) {
  Write-Host "[FAIL] windowed scale-path smoke exit $code (often Present DEVICE_REMOVED)"
  exit $code
}
Write-Host "[PASS] windowed scale-path smoke"

# Shared shader dir: Debug rebuild of lit_cube.ps.cso (SM6.6 bindless) breaks a stale Release exe.
$relSandbox = Join-Path $BuildDir "samples\Sandbox\Release\sample_sandbox.exe"
if ($Config -ne "Release" -and (Test-Path $relSandbox)) {
  Write-Host "== Release sample_sandbox smoke (shared shaders must match binary) =="
  & $relSandbox --backend=d3d12 --headless_frames=12
  if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] Release sandbox exit $LASTEXITCODE — rebuild Release after shader/root-sig changes"
    exit $LASTEXITCODE
  }
  Write-Host "[PASS] Release sandbox smoke"
}

if ($Golden) {
  $py = Get-Command python -ErrorAction SilentlyContinue
  if (-not $py) {
    Write-Host "[SKIP] python missing — skip golden"
    exit 0
  }
  Write-Host "== run_golden.py (Q1+Q3+C2: sandbox/depth/learn06/learn09) =="
  & python (Join-Path $root "tests\scripts\run_golden.py") --config $Config --build-dir $BuildDir --targets sandbox,sandbox_depth,learn06,learn09
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
  Write-Host "== run_matrix_smoke.py =="
  & python (Join-Path $root "tests\scripts\run_matrix_smoke.py") --config $Config --build-dir $BuildDir --backend d3d12
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

if ($Validation) {
  Write-Host "[PASS] Validation flag honored (layers when SDK provides; else device log SKIP/warn)"
}

exit 0
