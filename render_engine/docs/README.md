# 文档索引

> 实现与里程碑见 [PLAN.md](PLAN.md)；**当前迭代**看 [DOING_UNDO_TODO.md](DOING_UNDO_TODO.md)。入门：[GETTING_STARTED_M1.md](GETTING_STARTED_M1.md)。

## 怎么读

| 你想… | 先看 |
|---|---|
| 本轮 Doing / 回退 / 下一档 Todo | [DOING_UNDO_TODO.md](DOING_UNDO_TODO.md) |
| 知道做不做、边界在哪 | [POSITIONING.md](POSITIONING.md) → [KNOWN_GAPS.md](KNOWN_GAPS.md) |
| 工作区各层怎么切 | [../../docs/LAYERS.md](../../docs/LAYERS.md) · [../../docs/README.md](../../docs/README.md) |
| 外面怎么挂玩法/脚本/编辑器 | [HOSTING.md](HOSTING.md) |
| Host API / Prefab 契约 | [HOST_API.md](HOST_API.md) → [PREFAB_SCHEMA.md](PREFAB_SCHEMA.md) |
| 通用玩法壳 + 脚本 | [../../game_kit/docs/README.md](../../game_kit/docs/README.md) |
| 品类层 / 游戏工程 | [../../genre_kits/README.md](../../genre_kits/README.md) · [../../games/README.md](../../games/README.md) |
| 视口编辑器（**不在本目录**） | 工作区 `editor/docs`（见 [LAYERS](../../docs/LAYERS.md)） |
| 渲染可用 vs 游戏可用 vs 对标主流 | [PLAN.md](PLAN.md) **§1.9** → [game_kit PLAN](../../game_kit/docs/PLAN.md) |
| 按什么顺序实现、怎么验收 | [PLAN.md](PLAN.md) → [DOING_UNDO_TODO.md](DOING_UNDO_TODO.md) → [GETTING_STARTED_M1.md](GETTING_STARTED_M1.md) |
| 模块怎么切、目录长什么样 | [ARCHITECTURE.md](ARCHITECTURE.md) |
| 编码/依赖/双后端约定 | [STANDARDS.md](STANDARDS.md) → [THIRD_PARTY.md](THIRD_PARTY.md) → [VULKAN_PARITY.md](VULKAN_PARITY.md) |
| 离线烘焙与工具 | [TOOLING.md](TOOLING.md) |
| 怎么测 | [TESTING.md](TESTING.md)（分层 + **§8 自动化/人工/工具与覆盖水位**）；双后端比图见 [VULKAN_PARITY.md](VULKAN_PARITY.md) |
| 怎么调试调优 | [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md) |
| 怎么学 | [learn/README.md](learn/README.md) → [learn/PATH.md](learn/PATH.md) |
| 为何这样设计 | [learn/ADR_INDEX.md](learn/ADR_INDEX.md) |

## 产品文档

| 文档 | 说明 |
|---|---|
| [PLAN.md](PLAN.md) | 总验收、范围、风险锁死、里程碑、进度；**§3.1 测试加深**；**§1.9 游戏可用水位** |
| [DOING_UNDO_TODO.md](DOING_UNDO_TODO.md) | **当前迭代** Doing / Undo / Todo 工作板 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 分层、目录树、子系统、后端能力 |
| [POSITIONING.md](POSITIONING.md) | 是/不是、缺陷、风险锁死 |
| [KNOWN_GAPS.md](KNOWN_GAPS.md) | 缺口 ↔ 里程碑；含 **§4 M25 后候选**（含 C19–C21） |
| [HOSTING.md](HOSTING.md) | **玩法层 / 脚本 / 编辑器外挂接入** |
| [HOST_API.md](HOST_API.md) | **Host API v0 草案** |
| [PREFAB_SCHEMA.md](PREFAB_SCHEMA.md) | **场景/Prefab 共享 schema** |
| [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md) | **Cook/异步/线程分离/寿命/数据依赖与生命周期/GPU Profiling** |
| [TOOLING.md](TOOLING.md) | 必要/可后置/不做工具 |
| [STANDARDS.md](STANDARDS.md) | 编码、架构、通讯、L0/L1/L2、SoA |
| [THIRD_PARTY.md](THIRD_PARTY.md) | 可引入三方与抽象边界 |
| [TESTING.md](TESTING.md) | unit / integration / golden / CI；**§8 分工·水位·测法** |
| [VULKAN_PARITY.md](VULKAN_PARITY.md) | **D3D12 ↔ Vulkan 差异矩阵、缺口、Sandbox parity 剖面** |
| [MIXED_PICK.md](MIXED_PICK.md) | **M20** 2D/3D 拣选、高亮、IntegerScale |
| [SANDBOX_MCP.md](SANDBOX_MCP.md) | Harness 矩阵抽样（**保留冻结**）；MCP Cursor 适配（**不扩**） |
| [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md) | 调试·调优·排错 |
| [GETTING_STARTED_M1.md](GETTING_STARTED_M1.md) | **M1 落地清单（可执行）** |

## 教学文档

| 文档 | 说明 |
|---|---|
| [learn/README.md](learn/README.md) | 双轨模型 |
| [learn/PATH.md](learn/PATH.md) | 学习路径（含 M17–M25 占位章） |
| [learn/SAMPLES.md](learn/SAMPLES.md) | Sample 约定 |
| [learn/ADR_INDEX.md](learn/ADR_INDEX.md) | ADR 索引 |
| [learn/GLOSSARY.md](learn/GLOSSARY.md) | 术语 |
| [learn/BASICS.md](learn/BASICS.md) | 数学 / HLSL 速查 |
| [learn/DEBUG_WORKFLOW.md](learn/DEBUG_WORKFLOW.md) | 抓帧学习向 |
| [learn/chapters/](learn/chapters/README.md) | 章节正文（随实现补） |

## 一致性约定（避免再冲突）

1. **平台**：Windows = D3D12 + Vulkan；Linux = Vulkan only；**不做** macOS / 移动 / Metal。  
2. **落地顺序**：先 D3D12，再 Vulkan（ADR 0024）；文案写「经 RHI」，勿写死「仅接 D3D12」。  
3. **音频**：M7 起可解码+输出；**明确不做特效**（ADR 0013）。  
4. **视频**：随后端硬解；无软解 / 跨 API（ADR 0012）。  
5. **工具**：最小 CLI 见 TOOLING；**引擎内不做**可视化内容编辑器（ADR 0025）；可选外挂 `editor/`（C21）。  
6. **场景**：Node 树权威 + 渲染 SoA；**非默认 ECS**（ADR 0024）。  
7. **分层**：玩法不进引擎；`game_kit` 品类无关；品类 → `genre_kits`；内容 → `games`（ADR 0027 / **0028**、[LAYERS](../../docs/LAYERS.md)）。**编辑器规格/排期不进本目录**，只在工作区 `editor/docs`。  
8. **进度用语**：未写代码前缺口状态用 **已排期 / 未开始**，不用「补齐中」。

## 相关

- 工作区根 [README.md](../../README.md) · 工作区文档 [../../docs/README.md](../../docs/README.md)  
- 引擎说明 [../README.md](../README.md)  
- 工具占位 [../../tools/README.md](../../tools/README.md)  
