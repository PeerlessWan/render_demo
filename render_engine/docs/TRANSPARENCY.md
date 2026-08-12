# 透明策略（M11 骨架）

- Opaque：前到后或状态排序均可；当前 Sandbox 不区分。
- AlphaTest：仍写深度。
- AlphaBlend：后到前按相机距离排序；禁止与 Opaque 混在同一 bucket。
- 高级 OIT：未实现（P2）。

Sample：使用 `RenderScene` 实例列表自行分区即可；引擎不强制 GPU 透明 Pass，直到 RHI 完整。
