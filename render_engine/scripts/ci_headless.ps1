# CI helper: CPU headless unit tests + optional GPU offscreen Sandbox + golden.
param(
  [string]$Config = "Debug",
  [string]$BuildDir = "",
  [switch]$Golden
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
  $BuildDir = Join-Path $root "build"
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

if ($Golden) {
  $py = Get-Command python -ErrorAction SilentlyContinue
  if (-not $py) {
    Write-Host "[SKIP] python missing — skip golden"
    exit 0
  }
  Write-Host "== run_golden.py =="
  & python (Join-Path $root "tests\scripts\run_golden.py") --config $Config --build-dir $BuildDir
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

exit 0
