# 透明策略（M11）

## 规则

| Bucket | 深度写 | 排序 | 说明 |
|---|---|---|---|
| Opaque | 开 | 任意 / 状态排序 | 主路径 lit |
| AlphaTest | 开 | — | 未单独 PSO（可用 cutout 材质扩展） |
| AlphaBlend | **关** | **后到前**（相机距离） | `DrawTransparentLitCubes` |
| OIT | — | — | 未实现（P2） |

禁止 Opaque 与 AlphaBlend 混在同一 submit bucket。

## 引擎接线

- `material::PbrMaterial::transparent` + `ResolveMeshMaterial("glass")`
- `RenderSystem::DrawFrame`：拆分 opaque / transparent，透明按距离排序后 `DrawTransparentLitCubes`
- 阴影只画 opaque
- 顺序：Shadow → Opaque → Post(SSAO/TAA) → **Transparent** → UI

## Sandbox

- 节点 `glass`（`mesh_id = "glass"`）：半透明青蓝立方体，alpha≈0.35
