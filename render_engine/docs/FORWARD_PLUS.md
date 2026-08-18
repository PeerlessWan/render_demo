# Forward+ 路径（C01 / M26）

> 关联：[ADR 0032](learn/adr/0032-m26-forward-plus-cluster.md)、[ARCHITECTURE.md](ARCHITECTURE.md) §4.4、[ADR 0036](learn/adr/0036-mega-w9-deepen.md)

## 钉死结论

产品 **不透明 lit** 路径为 **Forward+**：

- 单次（或少量）HDR color + depth 写入；**无** deferred G-buffer（无 Albedo/Normal/ORM MRT 布局）。  
- 方向光 + CSM；局部点/聚光在 lit 像素着色器中累加（C02：最多 16 上传，≤2 Atlas 阴影）。  
- **Mega-W8/W9 C02**：`AssignLightsToTiles` 按 **range 球体投影 AABB** 扩格进 8×4 tile（非仅中心点）；`PackTileLightLists` → FrameCB；lit PS 按屏幕 UV 累加。  
- **Tile CS**：`shaders/hlsl/light_tile_cull_cs.hlsl`（及 `_vk`）输出同形 `tile_light_count[32]` / `tile_light_index[256]`；CPU `SimulateLightTileCullCs` / `CullLightsToTilesCpuReference` 与 CS 数学对齐。`RenderSystem` 在 `enable_tiled_lights` 时优先 `DispatchLightTileCull`，否则回退 CPU。  
- SSAO / TAA / SSR / Bloom / Fog / Tonemap 等在 **后处理** 消费 color/depth，不改变 Forward+ 主路径定义。

## FrameGraph Pass 名（冻结）

`RenderSystem::DrawFrame` 注册的 Pass **名称字符串**如下；业务/工具/文档应以表内名为准，勿随意改名。

| Pass 名 | 作用 |
|---|---|
| `ShadowCSM` | 方向光级联阴影 Atlas |
| `LocalShadow` | 局部光 Shadow Atlas（点光立方体面 / 聚光单面） |
| `OpaqueLit` | Forward+ 不透明 lit（含实例化提交） |
| `Transparent` | 透明队列（排序后 forward 混合） |
| `Skybox` | 天空盒 |
| `PostSSAO_TAA` | 屏幕空间后处理（SSAO/TAA/SSR/DoF/MB/Bloom/Fog/Tonemap + C04 vignette/grain） |
| `DebugLines` | 调试线 |
| `UI2D` | 屏幕空间 UI / 2D 叠加 |

资源逻辑名（读写边）：`ShadowMap`、`LocalShadowMap`、`Color`、`Depth`。

## 非目标（本波）

- Deferred / Hybrid deferred  
- Z-slice 完整 GPU 集群重写（本波仅屏幕 2D tile + range bin）  
- Mesh Shader 几何管线（见 C08）
