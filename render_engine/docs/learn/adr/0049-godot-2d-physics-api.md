# ADR 0049: Godot 级 2D API + Physics2D + 3D 物理加深

- 状态: Accepted（W26 已落地；对标见 ENGINE_VS，非 100%）
- 日期: 2026-08-21
- 关联: ADR 0015、0019、0048；ENGINE_VS_MAINSTREAM

## 背景

审计认定 2D 画面管线（无 UV 合批）与物理（无独立 2D、Joint 教学桩）相对 Godot 中端偏弱。用户要求 **API 级对齐** Godot 4 子集，完成后以源码再评真实 %。

## 决策

1. **2D 渲染**：CanvasItem/Node2D 树、Camera2D、Sprite UV 合批（`DrawTexturedQuads` / atlas）、Tile 真画、AnimationPlayer2D 子集。
2. **Physics2D**：新 `IPhysicsWorld2D`；默认 **builtin AABB**（Godot 体语义）；`CreateBox2DPhysicsWorld2D` 预留（当前 nullptr，可选后续 FetchContent）；CharacterBody2D / RigidBody2D / StaticBody2D / Area2D。
3. **Physics3D**：Jolt 加深层掩码、公开 ShapeCast、真 Hinge/Fixed/Slider/Point、角色 floor 状态、Area 事件；Vehicle/Shatter 仍教学桩。
4. **不做**：Navigation2D、Spine、GDScript、2D 材质节点图、载具轮胎产品化、服装 SoftBody。
5. **玩法边界**：ADR 0019 不变；本波只加引擎 API，不进任务/对话/战斗。

## 后果

- 优点：像素/平台玩法宿主可直接对齐 Godot 心智。
- 代价：双物理世界（2D+3D）；Box2D 真后端仍待可选接入。
- **对标（2026-08-21 复审）**：2D ≈80–88%；物理 ≈75–82%；整内核 vs Godot ≈60–72%。**禁止**标 100%。

## 收口备注

- 单测：`test_w26.cpp`；样例：`sample_41_platform_2d`。
- 宿主：`game_kit` `set_physics2d` + `physics2d_host.h`；editor Play 创建 Physics2D、2D 视口同步 Camera2D。
