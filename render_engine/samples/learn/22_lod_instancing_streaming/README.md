# Learn 22 — LOD、实例化与流式

## 目标

串联 `LodSelect`、`StreamingBudget`、`BuildInstanceBuffer` 与多实例场景渲染（M10 P0）。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_22_lod_instancing_streaming
build\samples\learn\22_lod_instancing_streaming\Debug\sample_22_lod_instancing_streaming.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `LodSelect::SelectLevel` | 距离 → LOD 级 |
| `StreamingBudget::Resident` | 内存预算 resident |
| `BuildInstanceBuffer` | CPU instance 打包 |

## 必做练习

1. 调小 budget 触发 `EvictIfNeeded`。
2. 增加 instance 数量，观察 draw count。
3. 阅读 `streaming_budget.cpp` 驱逐策略。

## 常见坑

- **GPU 间接绘制未接**：instance buffer 仅 CPU 侧演示。
- **LOD mesh 未切换**：当前同一 mesh_id。
