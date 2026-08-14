# Learn 19 — DXR 入门与降级

## 目标

调用 `CanRunDxrDemo` 探测 DXR 能力；**无 DXR 硬件也 exit 0**，打印可诊断状态。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_19_dxr_intro
build\samples\learn\19_dxr_intro\Debug\sample_19_dxr_intro.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `CanRunDxrDemo` | D3D12 + raytracing feature 门控 |
| `RtStatus::Resolve` | 启用/降级/不可用 |

## 必做练习

1. 在无 RT GPU 机器确认 exit 0 与日志。
2. 阅读 `raytracing.cpp` 降级分支。
3. 对比 `allow_fallback=false` 时 `EnsureSafe` 行为。

## 常见坑

- **非真 DXR 渲染**：仅能力探测，不建 AS/PSO。
- **Vulkan RT**：Resolve 支持但 CanRunDxrDemo 仅 D3D12。
