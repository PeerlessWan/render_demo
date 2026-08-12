# ADR 0019: 2D/像素混合渲染进引擎；玩法不进引擎

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH30, PLAN §1.4 / M16, engine/render2d, engine/tilemap

## 背景

2D/3D 混合像素风内容需要 Sprite、Tilemap、像素采样与排序等**渲染能力**。RPG 对话/背包/战斗等属于**玩法**，若塞进引擎会无限膨胀且与「通用渲染引擎」定位冲突。

## 决策

1. **M16 补齐** Render2D / Pixel / Tilemap 渲染与相机/排序约定。  
2. Tilemap **碰撞层数据**可供给物理，但不实现任务/对话/战斗。  
3. 玩法系统不进引擎：通用壳见 `game_kit`，品类可复用见 `genre_kits`，内容见 `games/<title>`（[LAYERS](../../../../docs/LAYERS.md)、ADR 0028）；可由 Module / 脚本驱动。

## 后果

- 优点：引擎可支撑像素混合画面；范围可控。  
- 代价：做完整 RPG 仍需外层玩法（非引擎内置）；分层见 LAYERS / ADR 0028。

## 学习提示

1. 先搞懂透明队列与 Y-sort，再叠 Tile 层。  
2. 像素模糊多半是过滤或非整数缩放，不是「分辨率不够」。  
