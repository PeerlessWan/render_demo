# CH38 — 大地形高度图 + ChunkStream（选修）

## 目标

加载 CC0/程序化高度图，理解 `LoadHeightmapPng` 与 `TerrainChunkStreamer` + `StreamingBudget` 的驻留环。

## 前提

CH22 流式预算；内容见 `content/scenes/large_terrain/`。

## 原理

半页收口：PNG → Heightmap → SampleHeight；按世界 XZ 切 chunk 并在预算内 load/unload。大图可 gitignore + 拉取脚本。不做 Nanite。

## 代码地图

- Sample：`samples/learn/38_large_terrain/`
- `engine/terrain/heightmap.h`（`LoadHeightmapPng`）
- `engine/terrain/chunk_stream.h`
- 对照 [PATH.md](../PATH.md) CH38 行

## 练习

1. 跑通 sample，记录 width/height 与 resident_count。  
2. 口头回答：ChunkStream 本波驻留的是什么（预算句柄 vs 整图 GPU mesh）？

## 常见坑

缺 `ENGINE_CONTENT_DIR_A`；把冒烟当成完整地形渲染验收。
