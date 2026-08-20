# ADR 0045: Mega-W22 Godot 渲染内核 ≈100%

- 状态: Accepted（**W22 已收口**；见 [DOING_UNDO_TODO.md](../../DOING_UNDO_TODO.md)）
- 日期: 2026-08-20
- 关联: ADR 0044、ENGINE_VS_MAINSTREAM、KNOWN_GAPS、gi/README

## 背景

W21 将「渲染内核 vs Godot 4」抬至约 80–85%。剩余缺口：SDFGI 观感深度、2D 阴影/遮挡、材质·粒子·天空产品化、超分设备绑定、Low 档弱端。

## 决策

1. **目标水位**：渲染内核 vs Godot **约 100%**（桌面 Forward+ 产品观感）；整引擎仍 ~35–45%。
2. **超分**：DLSS（NGX）+ FSR2（FidelityFX）设备绑定；链 **DLSS → FSR2 → builtin_bilinear**。**Intel XeSS 不做**。无 SDK/evaluate → 诚实 SKIP。
3. **本波做**：CascadeGi/SDF 加深；LightOccluder2D + 2D 阴影；PbrMaterial/粒子/天空·雾；Low 档弱端；导入默认小改。
4. **仍不做**：Nanite、真 NVIDIA DDGI、Lumen、Frame Generation、mac/Metal、引擎内脚本/复制、独立 Compatibility 渲染器、XeSS。

## 波次能力表

| 能力 | 层级 | 备注 |
|---|---|---|
| CascadeGi 隔帧远 cascade + 漏光抑制 | L0 | 非 RTXGI |
| 反射探针 × Probe GI 混合权重 | L0 | CPU `BlendWithReflection` |
| LightOccluder2D + 点光阴影 | L0 | 双端同波 |
| CanvasModulate | L0 | |
| PbrMaterial detail/triplanar | L0 | |
| 粒子吸引子 / trail | L0 | |
| 雾盒 AABB | L0 | Environment |
| DLSS/FSR BindUpscalerGpuDevice | L1 | 无 evaluate → bilinear |
| Quality Low 弱端 | L0 | |

## 后果

- 优点：可宣称桌面渲染内核对标 Godot ≈100%（自评口径）。
- 代价：厂商超分仍依赖本机 SDK + evaluate 接线；不宣称商标级 SDFGI 复刻。

## 收口备注（2026-08-20）

- CascadeGi：近 cascade 每帧、远隔帧；`leak_suppress`；`BlendWithReflection`。
- 2D：`LightOccluder2D` + `SampleOccluderShadow2D` + `CanvasModulate`。
- 超分：`BindUpscalerGpuDevice`；CreateUpscaler smoke；XeSS 外置。
- 单测：208 passed / 0 failed。
