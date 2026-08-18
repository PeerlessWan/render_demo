# C17 — 多窗口 / 多 GPU（钉死）

> 关联：[ADR 0036](learn/adr/0036-mega-w9-deepen.md)、[KNOWN_GAPS.md](KNOWN_GAPS.md) C17、看板 Todo「钉死」。

## 口径（本波及后续默认）

- **单窗口**：运行时只创建并呈现一个主窗口 / 主 swapchain。
- **单适配器**：RHI 绑定进程内**一个** GPU 适配器（D3D12 / Vulkan 各自一条设备路径）。
- **不实装**：多窗口合成、跨 GPU 共享资源、多 adapter 负载均衡、XR 立体多视口（见 C18）。

## 为何钉死

多窗口/多 GPU 属于特殊部署与产品分叉，会显著放大同步、呈现与资源生命周期复杂度，且与当前「渲染中台 + Sandbox 验收」主线收益不对齐。ADR 0036 明确本波只做文档钉死，不进 `engine/` 实装。

## 若产品需要

须新 ADR + PLAN 里程碑，单独验收双后端呈现与 Feature 门控；不得静默扩展现有 `IDevice` / Application 单例假设。
