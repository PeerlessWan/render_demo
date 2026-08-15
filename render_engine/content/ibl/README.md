# Environment / IBL content

| File | Notes |
|---|---|
| `ibl_pack.ibl1` | Irradiance + prefilter + BRDF LUT (IBL1) |
| `sky_kloppenheim06.sky1` | Display sky cubemap (SKY1, 128^2×6) |
| `src/kloppenheim_06_puresky_1k.hdr` | Source HDR (optional; bake input) |
| `lightmap.rgba` / `lightmap.meta.txt` | `lightmap_baker` RGBA8 bake (M8); runtime `gi::LoadLightmapRgba` |

**Sky / env source:** [Kloppenheim 06 Pure Sky](https://polyhaven.com/a/kloppenheim_06_puresky) — **CC0** (Poly Haven).

Bake:

```bat
ibl_baker content/ibl/src/kloppenheim_06_puresky_1k.hdr content/ibl 32 128
lightmap_baker content/ibl 64
```
