# ProbeVolume + Lightmap 共存（M22）

> **不是 DDGI / Lumen / RTXGI。** 本引擎的动态 GI 占位是 CPU `ProbeVolume`；烘焙侧是 `lightmap_baker` → `gi::LoadLightmapRgba`。

## 角色分工

| 路径 | 数据 | 运行时行为 | 开关 |
|---|---|---|---|
| **ProbeVolume** | 规则网格 irradiance；`UpdateFromLights` 帧预算增量 | 采样结果 **叠加** 到 `Environment.ambient`（Sandbox） | F1 面板 **Probe GI**（`enable_gi`） |
| **Lightmap** | `content/ibl/lightmap.rgba`（RGBA8 bake） | CPU `MultiplyAlbedoByLightmap` 后 `UploadLitAlbedoRgba` | F1 面板 **Lightmap**（`enable_lightmap`） |
| **IBL / Sky** | IBL pack + skybox | lit 着色器主环境光 | `enable_ibl` / `enable_skybox` |

三者 **独立**：关 Probe GI 不卸 lightmap；关 lightmap 不关掉探针 ambient。**任一都不宣称 DDGI。**

## Sandbox 约定

1. F1 打开 Effects 面板。
2. **Probe GI**：仅改 ambient tint；不替换 IBL、不写 lightmap UV。
3. **Lightmap**：对 slot0 albedo 做 bake 乘算再上传；关闭时恢复未乘算 albedo。
4. 两开关可同时开：探针动、lightmap 静，合成观感 = ambient 叠加 + albedo 烘焙暗角。

## Learn

见 [samples/learn/15_probes_gi/README.md](../../samples/learn/15_probes_gi/README.md) 知识点与代码地图。

## API

| 符号 | 说明 |
|---|---|
| `gi::ProbeVolume` | `set_enabled` / `UpdateFromLights` / `Sample` / W6 `RefineDensity` |
| `gi::LoadLightmapRgba` / `SampleLightmap` / `MultiplyAlbedoByLightmap` | M8 运行时 bake 路径 |
| `PbrMaterial::use_lightmap` | 材质标记（learn 15 示例） |

边界见 [ADR 0033](../learn/adr/0033-m27-w6-scene-scale.md)。
