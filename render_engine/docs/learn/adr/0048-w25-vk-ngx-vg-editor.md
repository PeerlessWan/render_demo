# ADR 0048: Mega-W25 VK 对齐 + NGX/RTXGI 实链 + VG 加深 + 编辑器

- 状态: Accepted（**合同已收口**；对标百分比见 2026-08-21 修订的 [ENGINE_VS_MAINSTREAM.md](../../ENGINE_VS_MAINSTREAM.md)）
- 日期: 2026-08-20（修订说明 2026-08-21）
- 关联: ADR 0047、VULKAN_PARITY、editor ADR 0002

## 背景

W24 曾宣称分域 vs Godot ≈100%（光追按 D3D12）。本波补：Vulkan RT **产品合同**、NGX/RTXGI **evaluate 链接纪律**、VirtualGeometry 加深、外挂编辑器中小关卡观感。

## 决策

1. **VK RT**：半分辨率软影 mask + 反射缓冲→SSR 与 D3D12 **同 Upload 合同**；无扩展 SKIP。VK 路径允许暗化/合成 stand-in（非 TraceRays 全帧）。视频硬解仍有意差。
2. **NGX/RTXGI**：有头+库时设 `ENGINE_*_EVALUATE_LINKED` 并可走 host 填充路径；**真 `EvaluateFeature` / SDK Update 待 drop-in**。无库则 SKIP→bilinear/CascadeGi。
3. **VG**：连续误差 LOD、驻留、Indirect、SW splat（Feature）；cull 当前为 **CPU CS 合同**（非 Nanite、非必真 GPU CS）。
4. **编辑器**：外挂 `editor/` 中小关卡 vs Godot **≈95%**（见 editor ENGINE_VS）；无 GDScript/材质节点图/导出。

## 后果

- 优点：双后端 RT **缓冲形状**对齐；厂商 SDK 可探测/链接；编辑器水位可诚实宣称。
- 代价：无本机 SDK/扩展时仍 SKIP；linked 时仍可能是 CPU nearest / 合成 atlas，**勿读成产品 DLSS/DDGI**。
- **对标：** 渲染内核 vs Godot **≈55–70%**（撤回一律 100%）。
