# ADR 0018: 测试分层与自动化

- 状态: Accepted
- 日期: 2026-08-12
- 关联: docs/TESTING.md, M1+

## 背景

渲染引擎回归若只靠人工点 Sandbox，无法支撑 M1–M18 体量。需要可重复的单测、集成与 GPU 自动化，同时承认无 GPU 的 CI 限制、双后端矩阵与「视频随后端硬解、无软解」语义。

## 决策

1. **三层**：unit（无 GPU）→ integration（部分 GPU）→ automation（冒烟 + 黄金图）。  
2. 框架默认 **Catch2 + CTest**；黄金图用脚本比对。  
3. PR 必过 unit；GPU 任务在具备硬件的 runner 上跑。  
4. 视频：当前后端无硬解能力 → **SKIP**；有能力但失败 → **FAIL**（D3D12VA / Vulkan Video 分别测）。  
5. 黄金图基线变更需人工批准。  
6. **不做像素/帧级全覆盖**：自动化锁逻辑 + 主路径存活 + 默认外观抽样；观感走人工；屏障/Pass/驱动走 Validation 与 PIX/RenderDoc。细则见 [TESTING.md §8](../../TESTING.md)。  
7. **Harness 保留、MCP 冻结**（[PLAN.md](PLAN.md) §3.21）：Harness 为矩阵抽样接口、不再加命令；`sandbox_mcp` 不扩、CI 不依赖。  
8. **测试加深准优先于广**（[PLAN.md](PLAN.md) §3.1）：确定性截帧 → VK 真读回 → Validation CI → 小场景/中间缓冲黄金图；不把 RMSE 收到 0、不追全组合。

## 后果

- 优点：回归可拦截；与排错文档互补。  
- 代价：需维护测试资产与（可选）自托管 GPU CI。

## 学习提示

1. 先为 math/config 写单测建立习惯。  
2. 黄金图失败先看环境再改基线。  
