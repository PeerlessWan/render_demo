# meshoptimizer (vendored)

W17 ADR 0041: sources under `src/` from [zeux/meshoptimizer](https://github.com/zeux/meshoptimizer) (MIT).

CMake sets `ENGINE_WITH_MESHOPTIMIZER=1` when `src/meshoptimizer.h` + `clusterizer.cpp` exist.
`MeshletizePreferMeshoptimizer` calls `meshopt_buildMeshlets`.

`meshoptimizer_src/` may be a shallow clone leftover; runtime uses `meshoptimizer/src/`.
