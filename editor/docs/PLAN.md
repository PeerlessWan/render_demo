# editor 规划

> **前置：** 引擎场景序列化 + Manifest（约 M8–M9）；拣选/DebugDraw（M4/M20）越好做视口。  
> 里程碑前缀 **ED**；**不阻塞** render_engine M1–M25。C20 轻量 CLI 可先行。  
> **产品优先级 P1**：可后于 `game_kit` **GK3**（手改 JSON 也能先发可玩切片）。  
> **本文是视口编辑器规格/排期的唯一位置**；不写入 `render_engine/docs`。引擎只保留 C20（轻量 CLI）与「独立 `editor/`」边界。工作区口径见 [引擎 PLAN §1.9](../../render_engine/docs/PLAN.md)（不含 ED 表）。  
> **对标主流缺口（权威）：** [GAPS.md](GAPS.md)。

## 0. 对标口径

| 口径 | 本层交付 | 缺陷 |
|---|---|---|
| **摆放器可用** | ED0–ED3 | 现全缺；见 GAPS §2 |
| **编辑器可用** | ED4–ED5 | 现全缺；Play / Prefab / 脚本字段 |
| **对标主流** | 不宣称 | 材质图 / UMG / 动画工具 / DCC — GAPS §3–§4，刻意不对齐 |

一期是关卡摆放器，不替代 Blender，也不替代 `game_kit` GK0–GK3。

## 1. 里程碑

| 里程碑 | 目标 | 主要交付 | 验收 |
|---|---|---|---|
| **ED0** 文档与空壳 | 可编译 | CMake、空窗口或复用引擎清屏作视口宿主 | 进程能起 |
| **ED1** 视口 MVP | 能看场景 | 加载引擎场景、编辑相机、网格 | 打开 Sample 场景可飞 |
| **ED2** 选择与变换 | 能改 | 点击选中、Gizmo、属性变换、Undo 基础 | 移物体 → 保存 → Runtime 一致 |
| **ED3** 层级与内容 | 能摆 | 场景树、内容浏览器、拖拽创建、保存完整 | 搭一个小关卡无手改 JSON |
| **ED4** Play | 能验 | Play-in-Editor、暂停、退出恢复 | 进 Play 可动，退出场景不脏（或可弃） |
| **ED5** Prefab/脚本 | 内容协作 | Prefab 放置；可选 game_kit 脚本字段 | 与 GK4 对齐 |
| **ED6** 打磨 | 可选 | 多视口、吸附、批量、cook 一键 | 单独立项 |

## 2. 建议时序

> ED 不替代 GK0–GK3。一期是关卡摆放器，不替代 Blender。

| 阶段 | 动作 |
|---|---|
| 引擎 M1–M7 | 仅文档；**C20 CLI 可先于视口**（场景 JSON 校验 / 依赖图） |
| 引擎 M8–M9 | ED0–ED1（可与 GK1 并行，不挡游戏可用） |
| 引擎 M10+/M20 拣选 | ED2–ED3 |
| **建议：GK3 之后** | ED1–ED2 视口摆物体、保存、Runtime 可读 |
| game_kit GK3–GK4 | ED4–ED5 |

## 3. 进度

| 里程碑 | 状态 |
|---|---|
| ED0–ED3 | 已写 |
| ED4 Play | 同进程快照 Play，退出恢复 |
| ED5 Prefab | 内容列表放置实例 + 脚本路径字符串 |
| ED6 | 未开始 |

## 4. 相关

- [GAPS.md](GAPS.md) — **对标主流 / 一期 / 刻意不做**  
- [ADR_INDEX.md](ADR_INDEX.md)  
- [FEATURES.md](FEATURES.md)  
- [../../render_engine/docs/PLAN.md](../../render_engine/docs/PLAN.md) **§1.9**  
- [../../game_kit/docs/PLAN.md](../../game_kit/docs/PLAN.md)（游戏可用主缺口，先于本层）  
- [../../render_engine/docs/KNOWN_GAPS.md](../../render_engine/docs/KNOWN_GAPS.md) C20/C21  
