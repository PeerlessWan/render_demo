# Learn 18 — 超分接入契约

## 目标

调用 `CreateUpscaler()` 与 CPU bilinear 占位实现，理解 Motion Vector / Jitter / DLSS·FSR 接入点（ADR 0008）。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_18_upscale
build\samples\learn\18_upscale\Debug\sample_18_upscale.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `CreateUpscaler()` | 工厂 |
| `IUpscaler::Upscale` | 4×4 → 8×8 双线性 |

## 必做练习

1. 实现相同尺寸 pass-through 分支测试。
2. 阅读 `upscaler.cpp` 插值核。
3. 思考 jitter 与 history buffer 如何喂给厂商 SDK。

## 常见坑

- **纯 CPU 占位**：非 DLSS/FSR 真实现。
- **无需 GPU**：本 sample 不创建 Application。
