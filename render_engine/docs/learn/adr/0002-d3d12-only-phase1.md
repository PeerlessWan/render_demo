# ADR 0002: 为何一期仅 D3D12

- 状态: **Superseded by [ADR 0020](0020-windows-d3d12-vulkan-linux-vulkan.md)**
- 日期: （历史）
- 关联: CH01、CH06

## 背景

项目启动时优先在 Windows 上用单一后端降低 RHI/FrameGraph 落地成本。

## 原决策（已废弃）

一期仅实装 D3D12；Vulkan 等仅 stub。

## 现状

见 **ADR 0020**：Windows = D3D12 + Vulkan；Linux = Vulkan。
