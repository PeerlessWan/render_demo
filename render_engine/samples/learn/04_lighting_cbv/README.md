# Learn 04 — Lighting CBV

## 目标

直接使用 RHI 层的 **`SetFrameLighting` + `DrawLitCube`**，理解每帧常量缓冲（CBV）如何携带 view/proj、太阳方向与环境光。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_04_lighting_cbv
build\samples\learn\04_lighting_cbv\Debug\sample_04_lighting_cbv.exe
build\samples\learn\04_lighting_cbv\Debug\sample_04_lighting_cbv.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 位置 |
|---|---|
| `FrameLighting` | `engine/rhi/i_device.h` |
| `SetupLitMesh` | 首帧初始化 PSO + 默认立方体 |
| `SetFrameLighting` | 每帧写入光照 CBV |
| `DrawLitCube` | 单实例 draw call |

## 必做练习

1. 修改 `sun_direction`，观察高光位置变化。
2. 把 `enable_shadows` 设为 `true` 并补全 shadow pass 调用链（对比 Sample 10）。
3. 打印 `lighting.view_proj.m[0]` 确认矩阵每帧更新。

## 常见坑

- **`SetupLitMesh` 只做一次**：重复调用可能重建 PSO，拖慢启动。
- **Headless**：stub 设备计数 draw，不校验 CBV 内容；GPU 路径才看得到光照变化。
- **Aspect 为 0**：窗口未就绪时用 1.f 兜底，避免除零。
