# editor 规划

> **前置：** 引擎场景序列化 + Manifest（约 M8–M9）；拣选/DebugDraw（M4/M20）越好做视口。  
> 里程碑前缀 **ED**；**不阻塞** render_engine M1–M25。C20 轻量 CLI 可先行。

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

| 阶段 | 动作 |
|---|---|
| 引擎 M1–M7 | 仅文档；可并行 C20 CLI |
| 引擎 M8–M9 | ED0–ED1 |
| 引擎 M10+/M20 拣选 | ED2–ED3 |
| game_kit GK3–GK4 | ED4–ED5 |

## 3. 进度

| 里程碑 | 状态 |
|---|---|
| ED0–ED6 | 未开始（文档先行） |

## 4. 相关

- [FEATURES.md](FEATURES.md)  
- [GAPS.md](GAPS.md)  
- [../../render_engine/docs/KNOWN_GAPS.md](../../render_engine/docs/KNOWN_GAPS.md) C20/C21  
