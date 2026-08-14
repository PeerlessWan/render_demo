# Golden baselines

Store `.rgba` dumps (`u32 width`, `u32 height`, then `width*height*4` RGBA8).

| Target | Baseline | Notes |
|---|---|---|
| `sandbox` | `sandbox_gpu_headless.rgba` | LDR color (Q1) |
| `sandbox_depth` | `sandbox_gpu_headless_depth.rgba` | depth viz；更严 RMSE/max-abs (Q3) |
| `learn06` | `learn_06_rhi_triangle.rgba` | C2 |
| `learn09` | `learn_09_pbr_ibl.rgba` | C2 |
| `matrix default` | `matrix_d3d12_default.rgba` | C3 harness capture |
| `matrix taa_off` | `matrix_d3d12_taa_off.rgba` | C3 |
| `matrix shadows_off` | `matrix_d3d12_shadows_off.rgba` | C3 |
| (C6) | `../shader_hashes.json` | DXIL/SPIR-V sha256；`check_shader_hashes.py --approve` |

```bat
python tests/scripts/run_golden.py --approve
python tests/scripts/run_golden.py --targets sandbox,sandbox_depth,learn06,learn09
python tests/scripts/run_matrix_smoke.py --approve
python tests/scripts/run_matrix_smoke.py
python tests/scripts/check_shader_hashes.py --approve
python tests/scripts/run_backend_parity.py
```

CI: `scripts/ci_headless.ps1 -Golden`（可选 `-StrictParity`）。Missing baseline → SKIP (not fail).
C4：默认 ROI + 松闸（RMSE≤90）期望 PASS；`--strict` / `-StrictParity` 用紧闸 48（当前≈74 仍 FAIL，可选）。
Sandbox 黄金图默认 `--roi-ignore-hud`（Q5）。
