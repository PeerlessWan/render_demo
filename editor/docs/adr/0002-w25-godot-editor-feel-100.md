# ADR 0002: W25 编辑器对标 Godot 中小关卡观感

- 状态: Accepted（2026-08-21 修订自评）
- 日期: 2026-08-20
- 关联: [GAPS.md](../GAPS.md)、[FEATURES.md](../FEATURES.md)、引擎 ADR 0048

## 背景

ED0–ED6 能力已闭环。用户要求编辑器对标 Godot 中小关卡观感。

## 决策

1. **尺子**：中小 PC **关卡/Prefab 编辑器产品观感**（视口、树、检视、内容、Play、Prefab、地形/Tile/Anim/Bake）。
2. **宣称**：自评 **≈95%**（该尺子）；见 [ENGINE_VS_GODOT_EDITOR.md](../ENGINE_VS_GODOT_EDITOR.md)。不虚标全能 100%。
3. **不宣称**：GDScript、材质节点图、UMG、一站式 FBX、导出/插件商店（GAPS §4）。
4. **已知天花板**：热重载 albedo 仅设备 0/1 两槽；Anim 不绑骨骼蒙皮编辑器。

## 本波交付

快捷键/布局默认、输出控制台、保存打开反馈；ED-G 真实现波补正交视口、`ApplyOp` Undo Upload、文档诚实水位；单测不回退。
