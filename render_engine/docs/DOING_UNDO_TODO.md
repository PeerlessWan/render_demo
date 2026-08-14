# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 主线水位（截至 ABC wave）

| 层 | 状态 |
|---|---|
| **M1–M25 无 vendor** | 已收口（`c9977b4`） |
| **ABC 并行波** | **本档**：Q3 depth + C2 learn 黄金图；Cull→Indirect UAV；Bindless PS 堆槽采样；VK 采样 scene_color；RmlUi vendor 薄适配；OpenSSL HTTPS 正向测 |
| **测试门禁** | `ci_headless.ps1 -Golden`（sandbox / depth / learn06 / learn09）+ matrix；窗口 scale 冒烟 90 帧 |
| **仍后置** | C3 矩阵比图、全 Bindless 材质、完整 RML 皮肤、Linux M18、Harness/MCP 扩 |

```text
安全基线：c9977b4
本档 Doing：W-abc-wave
```

---

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空） | 待选定下一档 | 开 Doing 前先锁口径 |

---

## Todo（下一档候选）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| **P1** | T-c3-matrix-img | 矩阵格升级为比图（D3D12 默认/TAA off/阴影 off） | 依赖现有 `capture`；不扩命令 |
| P2 | T-vk-sample-deepen | VK 局部影/IBL 采样对标再加深 | 见 [VULKAN_PARITY.md](VULKAN_PARITY.md) |
| P2 | T-rmlui-doc | 完整 RML 文档/皮肤路径 | 本档仅薄适配 + vendor |
| P3 | T-linux-m18 | Linux + Vulkan | 外置（见 LINUX_VULKAN.md） |
| P3 | T-m14-bindless-full | 全 Bindless 迁移 | 明确后置 |
| P3 | T-ground-slab | 悬浮浅色层切物体 | **搁置** |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `c9977b4`（无 vendor 100% + Learn 2A + Sandbox hang 修复） |
| 上一基线 | `4b2f5f5`（四轨 100%） |

验证：`scripts/ci_headless.ps1 -Golden`；窗口 `--headless_frames=90`；unit；Sandbox 手动开窗不白屏卡死

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **W-abc-wave** | Q3 depth + C2 learn 黄金图；Cull UAV→ExecuteIndirect；Bindless PS 堆槽；VK 采样 scene_color；RmlUi 6.0 vendor 薄适配；OpenSSL HTTPS 正向测（无 SDK SKIP） |
| W-m100-nv | 无 vendor 100% + Learn 2A；Q1–C1；VK 读回/post RT；HiZ Cull CS；Bindless Feature |
| W-sandbox-hang | post 后 DrawLitInstanced → DEVICE_REMOVED；改 OpaqueLit + 实例缓冲双缓冲 + 窗口冒烟 |
| Track A–D | 四轨 100% |
| W-a-test / golden / matrix | 已落地；Harness 冻结；MCP 不进门禁 |
