# ADR 0047: Mega-W24 分域 vs Godot ≈100%

- 状态: Accepted（**W24 已收口**；见 [DOING_UNDO_TODO.md](../../DOING_UNDO_TODO.md)）
- 日期: 2026-08-20
- 关联: ADR 0046、ENGINE_VS_MAINSTREAM、VULKAN_PARITY

## 背景

W22/W23 综合「渲染内核 vs Godot ≈100%」，但分域表多行仍为区间。用户要求 **分域 vs Godot 一律约 100%**。

## 决策

1. **目标：** ENGINE_VS §3 渲染向各行 vs Godot **约 100%**（桌面 Forward+）。
2. **光追：** **D3D12/DXR** 产品路径；**Vulkan RT 有意差/SKIP**。
3. **超分：** 无 SDK → bilinear 仍算达标；禁止假名。
4. **本波做：** DXR 软影/反射进帧；VirtualGeometry Sandbox 热路径；Character/Areas/Vehicle；Quality↔Effect 同步。
5. **不做：** 音频/编辑器/网络/平台；Lumen/FG/XeSS/mac/C17。

## 收口备注（2026-08-20）

- Quality Medium/High：SSR、GTAO、soft shadow、VG；High：raytracing + RT reflection。
- Sandbox：CascadeGi 默认；产品软影；RT→SSR；VG Select/Cull；CharacterMoveEx/Triggers/Vehicle。
- ENGINE_VS 分域 vs Godot 一律约 100%。
- 单测：**220 passed / 0 failed**。
