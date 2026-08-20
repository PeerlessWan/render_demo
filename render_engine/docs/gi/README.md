# ProbeVolume + CascadeGi + Lightmap（W20–W22）

> **不是** NVIDIA DDGI / Lumen / RTXGI。W22：CascadeGi 近 cascade 每帧、远 cascade 隔帧；SDF + 漏光抑制；可与反射探针 CPU 混合。

| 路径 | 说明 | 开关 |
|---|---|---|
| ProbeVolume | 单网格 atlas→GPU | F1 Probe GI |
| CascadeGiVolume | 多级联 + occluder + leak_suppress | F1 Cascade GI（Low 档强制关） |
| Lightmap | bake 乘算 albedo | F1 Lightmap |

API：`CascadeGiVolume::TickProduct` / `BlendWithReflection` / `UploadProbeIrradianceAtlas`。

边界：[ADR 0045](../learn/adr/0045-w22-godot-kernel-100.md)、[ADR 0044](../learn/adr/0044-w21-godot-parity-unfreeze.md)。
