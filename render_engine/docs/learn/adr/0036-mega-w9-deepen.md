# ADR 0036: Mega-W9 全主题加深、尾巴、学习轨与文档审计

- 状态: Accepted
- 日期: 2026-08-17
- 关联: PLAN M27+ / Mega-W9、KNOWN_GAPS、ADR 0030–0035、DOC_AUDIT

## 背景

Mega-W8（ADR 0035）收口后，用户要求五主题全做并收口引擎尾巴，同时补齐学习 Sample/章节并梳理文档冲突。本 ADR 冻结本波边界与验收口径。

## 决策（落地口径）

1. **C02**：GPU `light_tile_cull_cs`（D3D12+VK）写同形 FrameCB tile 列表；CPU/CS **range 扩格**；≤16 灯、8×4、≤8/tile；不做 Z-slice 集群重写。
2. **C08**：D3D12 真 MS PSO + `DispatchMesh`；VK `VK_EXT_mesh_shader` 探测对齐，有扩展则最小示范否则 SKIP；cook 优先 meshoptimizer，否则 AABB。
3. **C03**：Light Function 最小调制（纹理/程序因子乘局部灯）；可关。
4. **C06/C07**：VT GPU feedback + 页上传（非 Nanite/非默认全材质）；最小 billboard HLOD；地形/植被 chunk + StreamingBudget。
5. **GI/RT**：Probe atlas 主帧加深（非 DDGI）；DXR 小分辨率可选合成；VK TraceRays 示范或 SKIP。
6. **角色**：主 `IDevice` GPU 蒙皮可见路径；SM crossfade；BlendTree/SM 最小序列化。
7. **平台**：Sandbox 热更真重建 PSO + 贴图 Reload；MsQuic loopback 可靠流（缺库 SKIP）；Linux headless + 最小 X11 clear。
8. **尾巴**：Path2D 闭合简单填充；C17 **文档钉死**不实装多窗口/多 GPU。
9. **学习轨**：补 PATH 缺 Sample + CH36；必修章节正文；DOC_AUDIT 冲突表。
10. **仍外置**：Nanite、真 DDGI、材质节点图、蓝图、XR、mac/移动、SVG 布尔、服装布料、Frame Generation。

## 文档修订

- 冲突处理优先级见 [DOC_AUDIT.md](../DOC_AUDIT.md)。
- ADR 0030/0032/0034/0035 中被本波加深的 SKIP/stub 条款以本 ADR 为准（旧文加注记）。

## 后果

- 单测：`test_m32` / `test_m33`（`[w9]`）。
- 看板 Mega-W9；KNOWN_GAPS/PLAN/PATH 对齐。
