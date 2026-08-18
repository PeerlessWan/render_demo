# large_terrain — 大场景高度图

## 源说明

| 文件 | 说明 |
|---|---|
| `heightmap_512.png` | **本仓自带**：512×512 灰度 PNG，程序化多山丘/山脊（CC0）；约 50KB |
| `LICENSE.txt` | **CC0**（生成物，无第三方原图） |
| `download_large_terrain.ps1` | 本地再生成；或 `-FetchUrl` 拉取你信任的灰度 PNG |

## Sandbox

F1 → **Large terrain heightmap**：加载本图、上传可见地形网格、相机移到俯瞰位置。
此前若只勾选却看不到场景，是因为旧逻辑只改了高度采样、没有画网格，且 PNG 曾漏提交。

## 引擎用法

- `engine::terrain::LoadHeightmapPng(path, cell, height_scale)` → `Heightmap`
- 与 `TerrainChunkStreamer` + `StreamingBudget` 配合：按相机 XZ 做 chunk 驻留（见 Learn `38_large_terrain`）

默认 `cell=1` 时世界边长约 511m；可按需放大 `cell` / `height_scale`。

## 体积策略

- ≤2MB 的 512（或 1024）图可直接进 git。
- 更大 CC0 原图建议 gitignore + 用 `download_large_terrain.ps1` 拉取（ADR 0037）。
