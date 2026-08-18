# CH35 — 高级 GI / 地形水体植被 / GPU Driven / VK RT（选修）

## 目标

分清 M22–M25 各能力落在哪条路径、哪个 Feature/Sandbox 开关，以及学习 Sample 与产品验收的分工。

## 前提

必修结束；建议已做 CH15、CH22、CH19、CH32。

## 原理

本主题**不单靠一个 sample**，按能力拆开：

| 能力 | 建议入口 | Feature / 开关 |
|---|---|---|
| 动态/探针 GI + Lightmap 共存（非 DDGI） | `samples/learn/15_probes_gi`；Sandbox **F1 → Probe GI / Lightmap** | ProbeVolume ambient 叠加；`use_lightmap` |
| 地形 / 水体 / 植被密度 | **Sandbox**（M23 heightmap + water + ScatterVegetation） | **F1 质量档** Low/Med/High → `vegetation_cap` |
| GPU Driven / 间接绘制 | `22_lod_instancing_streaming`、`27_gpu_submit_mt` | `execute_indirect` / 实例缓冲 |
| Meshlet / Mesh Shader | `36_w9_deepen`（`TryMeshShaderPath`） | Feature `meshlet` / `mesh_shader` |
| DXR / VK RT | `19_dxr_intro`；Vulkan 对照 `32_vulkan_backend` | Feature `raytracing`；无硬件 SKIP |
| Tile lights / VT 等 W9 加深 | Sandbox + ADR 0036；冒烟 `36_w9_deepen` | 见 ADR 与 KNOWN_GAPS |

L 级以 `FeatureSet` / 文档为准，勿把选修实验默认成 L0。

## 代码地图

- Sandbox：`samples/Sandbox/main.cpp`（F1 面板、地形、Probe、间接绘制）
- `samples/learn/15_probes_gi`、`22_*`、`27_*`、`19_*`、`32_*`、`36_w9_deepen`
- ADR：[0036-mega-w9-deepen.md](../adr/0036-mega-w9-deepen.md)

## 练习

1. 在 Sandbox 切换 F1 Probe GI / Lightmap / 质量档，观察日志与画面。  
2. 跑 `36_w9_deepen`，记录本机 Ok vs Unavailable。

## 常见坑

把「Sandbox 能开」当成「学习 Sample 已单独覆盖」；CH35 是索引章，细节回链各 sample README。
