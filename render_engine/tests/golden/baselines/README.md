# Golden baselines

Store `.rgba` dumps (`u32 width`, `u32 height`, then `width*height*4` RGBA8).

| Target | Baseline | Notes |
|---|---|---|
| `sandbox` | `sandbox_gpu_headless.rgba` | LDR color (Q1) |
| `sandbox_depth` | `sandbox_gpu_headless_depth.rgba` | depth viz；更严 RMSE/max-abs (Q3) |
| `learn06` | `learn_06_rhi_triangle.rgba` | C2 |
| `learn09` | `learn_09_pbr_ibl.rgba` | C2 |

```bat
python tests/scripts/run_golden.py --approve
python tests/scripts/run_golden.py --targets sandbox,sandbox_depth,learn06,learn09
```

CI: `scripts/ci_headless.ps1 -Golden`. Missing baseline → SKIP (not fail).
