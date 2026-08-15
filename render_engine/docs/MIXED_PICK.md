# Mixed pick + highlight + integer scale（M20）

> 统一 2D/3D 拣选、高亮与像素多 DPI 整数缩放约定。实现：`engine/mixed/pick.h`；Sandbox 已用；learn `30_pixel_hybrid` 演示 `IntegerScale`。

## Pick

`engine::mixed::Pick(instances, sprites, query)`：

1. 屏幕点 → NDC → 用 `inv_view_proj` 还原世界射线。
2. **优先 3D**：射线 vs 实例 `world_bounds` AABB，取最近命中 → `PickHit::Kind::Scene3D` + `node`。
3. **否则 2D**：从后往前点测 sprite 屏幕矩形 → `Sprite2D` + `sprite_index`（排序后列表顶层优先）。

`PickQuery` 需要：`screen_px`、`viewport_w/h`、`inv_view_proj`。

## Highlight

Sandbox：LMB 短按（非拖视）触发 Pick；命中 3D 节点时用 `DebugDraw::AddAabb` 画琥珀色包围盒。松开后保持选中直到再次点击空白或无效。

精灵命中可同样用颜色/描边高亮（样例侧重 3D AABB；API 已返回 `sprite_index`）。

## IntegerScale（多 DPI）

```text
IntegerScale(window_w, window_h, design_w, design_h)
  = max(1, min(window_w/design_w, window_h/design_h))  // 整数除
```

用于像素风：内部以设计分辨率渲染，再按整数倍放大到窗口，避免非整数缩放糊边。  
`30_pixel_hybrid` 以 320×180 为设计分辨率打日志；Sandbox 可同样套用到 sprite/UI 像素层。

## 相关

| 位置 | 作用 |
|---|---|
| `engine/mixed/pick.h` / `pick.cpp` | Pick + IntegerScale |
| `samples/Sandbox/main.cpp` | 点击拣选 + AABB 高亮 |
| `samples/learn/30_pixel_hybrid` | IntegerScale + Tiled（M16） |
| learn PATH CH34 | 混合打磨选修章 |

拣选不替代物理 Raycast；仅渲染/调试向统一命中。
