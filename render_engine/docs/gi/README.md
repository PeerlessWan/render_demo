# ProbeVolume + Lightmap 共存（M22 / W20）

> **不是** NVIDIA DDGI / Lumen / RTXGI。动态 GI 为 **DDGI-lite**：CPU `ProbeVolume` + **W20** irradiance atlas → GPU（lit t11）空间采样；烘焙侧是 `lightmap_baker` → `gi::LoadLightmapRgba`。

## 角色分工

| 路径 | 数据 | 运行时行为 | 开关 |
|---|---|---|---|
| **ProbeVolume** | 规则网格 irradiance；`UpdateFromLights` 帧预算增量 | W20：atlas→GPU + lit 采样；亦可叠加 `Environment.ambient`（Sandbox） | F1 **Probe GI**（`enable_gi`）；质量档 `probe_update_budget` |
| **Lightmap** | `content/ibl/lightmap.rgba`（RGBA8 bake） | CPU `MultiplyAlbedoByLightmap` 后 `UploadLitAlbedoRgba` | F1 **Lightmap**（`enable_lightmap`） |
| **IBL / Sky** | IBL pack + skybox | lit 着色器主环境光 | `enable_ibl` / `enable_skybox` |

三者 **独立**：关 Probe GI 不卸 lightmap；关 lightmap 不关掉探针。**任一都不宣称真 DDGI。**

## Sandbox 约定

1. F1 打开 Effects 面板。
2. **Probe GI**：W20 走 atlas→GPU；亦可改 ambient tint；不替换 IBL、不写 lightmap UV。
3. **Lightmap**：对 slot0 albedo 做 bake 乘算再上传；关闭时恢复未乘算 albedo。
4. 两开关可同时开：探针动、lightmap 静。
5. 默认质量 **Medium**；高预算在 High 档。

## Learn

见 [samples/learn/15_probes_gi/README.md](../../samples/learn/15_probes_gi/README.md) 知识点与代码地图。

## API

| 符号 | 说明 |
|---|---|
| `gi::ProbeVolume` | `set_enabled` / `UpdateFromLights` / `Sample` / W6 `RefineDensity` / W10 `BlendNeighborhood`·`CascadeRefine` |
| `gi::LoadLightmapRgba` / `SampleLightmap` / `MultiplyAlbedoByLightmap` | M8 运行时 bake 路径 |
| `IDevice::UploadProbeIrradianceAtlas` | W20 atlas→GPU（t11） |
| `PbrMaterial::use_lightmap` | 材质标记（learn 15 示例） |

边界见 [ADR 0043](../learn/adr/0043-w20-product-deepen.md)、[ADR 0033](../learn/adr/0033-m27-w6-scene-scale.md)。
