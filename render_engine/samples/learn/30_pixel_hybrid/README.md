# Learn 30 — 2D / 像素混合

## 目标

`SortSprites` + 将 sprite 列表传入 `RenderSystem::DrawFrame`，理解 Nearest 采样与 Y-sort（M16）。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_30_pixel_hybrid
build\samples\learn\30_pixel_hybrid\Debug\sample_30_pixel_hybrid.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `render2d::Sprite` | 2D 绘制描述 |
| `SortSprites` | layer + sort_y |
| `DrawFrame(..., &sprites)` | 3D+2D 混合 |

## 必做练习

1. 改 `nearest=false` 对比缩放。
2. 调整 `sort_y` 观察覆盖顺序。
3. 尝试 `LoadTiledJson` 加载 tilemap。

## 常见坑

- **Atlas 占位**：`atlas_id` 无真实纹理时 headless 仍计数。
- **整数缩放**：窗口模式才可见像素 crisp 效果。
