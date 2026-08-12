# ADR 0003: 为何引入 RHI 而不是业务直调 D3D12

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH06, engine/render/rhi, backends/*

## 背景

业务直调 D3D12 会锁死后端，无法在 Windows 上切 Vulkan、也无法支撑 Linux。

## 决策

1. 公开 API 只依赖 **RHI**；D3D12/Vulkan 细节仅在 `backends/*`。  
2. 落地顺序：先 D3D12，再 Vulkan（ADR 0020 / 0024）。  
3. D3D11/GL/GLES 仅 stub，不实装。

## 后果

- 优点：双后端可替换；教学可对照抽象动机。  
- 代价：多一层；能力差用 Feature / L0–L2 管理。
