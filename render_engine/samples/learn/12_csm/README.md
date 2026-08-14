# Learn 12 — CSM 多级联

## 目标

开启 **4 级 CSM 级联阴影**，对比 CH10 单 cascade 的覆盖范围与稳定性。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_12_csm
build\samples\learn\12_csm\Debug\sample_12_csm.exe --headless --headless_frames=2
```

## 代码地图

| 设置 | 值 |
|---|---|
| `EffectTuning::shadow_cascades` | 4 |
| `RenderSystem::cascade_count()` | 运行时级联数 |

## 必做练习

1. 把 cascade 改为 2，观察远处阴影分辨率变化。
2. 调整相机高度，理解 split 距离对阴影稳定性的影响。
3. 阅读 `shadow_csm.h` 中 split 计算。

## 常见坑

- **Headless**：stub/GPU 路径均计数 shadow pass；画面差异需窗口模式。
- **依赖 CH10**：先理解单 cascade 再开多级联。
