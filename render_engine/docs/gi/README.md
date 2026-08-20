# ProbeVolume + CascadeGi + Lightmap 共存（M22 / W20 / W21）

> **不是** NVIDIA DDGI / Lumen / RTXGI。动态 GI 为 **DDGI-lite / SDFGI-lite**：CPU `ProbeVolume` 或 W21 `CascadeGiVolume` + irradiance atlas → GPU（lit t11）空间采样；烘焙侧是 `lightmap_baker` → `gi::LoadLightmapRgba`。

## 角色分工

| 路径 | 数据 | 运行时行为 | 开关 |
|---|---|---|---|
| **ProbeVolume** | 规则网格 irradiance；`UpdateFromLights` 帧预算增量 | atlas→GPU + lit 采样 | F1 **Probe GI** |
| **CascadeGiVolume**（W21） | 近密远疏多级联 + AABB 软遮挡 | 主 cascade atlas→GPU；Godot SDFGI 精神 | F1 **Cascade GI** |
| **Lightmap** | `content/ibl/lightmap.rgba`（RGBA8 bake） | CPU `MultiplyAlbedoByLightmap` 后上传 | F1 **Lightmap** |
| **IBL / Sky** | IBL pack + skybox | lit 着色器主环境光 | `enable_ibl` / `enable_skybox` |

独立开关；**任一都不宣称真 DDGI / SDFGI 商标级对等**。

## Sandbox 约定

1. F1 打开 Effects 面板。  
2. **Probe GI** / **Cascade GI**：二选一优先 Cascade；默认 OFF 保黄金图。  
3. **Lightmap**：slot0 albedo bake 乘算。  
4. 默认质量 **Medium**；高预算在 High 档。

## API

| 符号 | 说明 |
|---|---|
| `gi::ProbeVolume` | `TickProduct` / `Sample` / `CascadeRefine` / atlas |
| `gi::CascadeGiVolume` | 多级联 + `set_occluders` SDF-lite + atlas |
| `IDevice::UploadProbeIrradianceAtlas` | atlas→GPU（t11） |
| `PbrMaterial::use_lightmap` | 材质标记 |

边界见 [ADR 0044](../learn/adr/0044-w21-godot-parity-unfreeze.md)、[ADR 0043](../learn/adr/0043-w20-product-deepen.md)。
