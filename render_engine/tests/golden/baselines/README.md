# Golden baselines

Store `.rgba` dumps (`u32 width`, `u32 height`, then `width*height*4` RGBA8).

Generate once on a reference GPU:

```bat
python tests/scripts/run_golden.py --approve
```

CI compares with `ENGINE_GOLDEN_TESTS=ON` or `scripts/ci_headless.ps1 -Golden`.
Missing baseline → SKIP (not fail).
