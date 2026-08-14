# Learn 08 — Material Variants

## 目标

通过不同 **`mesh_id`**（`cube` / `metal` / `glass`）触发 `ResolveMeshMaterial` 中的 PBR 参数变体，理解「网格 ID → 材质表」的间接映射。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_08_material_variants
build\samples\learn\08_material_variants\Debug\sample_08_material_variants.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 文件 |
|---|---|
| `ResolveMeshMaterial` | `engine/render/local_lights.cpp` |
| `MeshRenderer::mesh_id` | `engine/scene/world.h` |
| `DrawFrame` | 按 instance.mesh_id 解析材质并提交 draw |

## 必做练习

1. 在 `ResolveMeshMaterial` 增加 `"copper"` 变体并在场景中放置。
2. 对比 `metal` 与 `glass` 的 `transparent` 标志对 draw 顺序的影响。
3. 启动时阅读日志中的 metallic/roughness 是否与预期一致。

## 常见坑

- **mesh_id 字符串拼写**：大小写敏感，未知 ID 回退默认材质。
- **glass 半透明**：需透明 pass；headless stub 仅计数。
- **纹理路径**：`ground` 等 ID 可能引用 content 纹理；本 sample 用纯色变体即可。
