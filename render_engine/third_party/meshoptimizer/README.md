# meshoptimizer (optional stub)

This directory is an **optional** clone target for [zeux/meshoptimizer](https://github.com/zeux/meshoptimizer).

## Status

- **Not vendored** by default — the engine does not download packages at configure time.
- `engine::gpu_driven::MeshletizePreferMeshoptimizer` falls back to the AABB grid cook when
  headers/libs are absent (same contract as `MeshletizeAabbGrid`).
- To opt in: run `tools/fetch_meshoptimizer.ps1` (documents `git clone`; network failures are OK).

## After clone

Expected layout (typical):

```text
third_party/meshoptimizer/
  src/
  LICENSE.md
  ...
```

Wire into CMake only when you intentionally enable a meshoptimizer build option (future);
until then Prefer* APIs remain AABB-safe.
