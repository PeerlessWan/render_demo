# 教学封装层

本目录是引擎之上的 **学习封装（Learn Layer）**：不改变产品引擎架构，只规定「怎么学、怎么练、怎么观察」。

产品实现仍以 [ARCHITECTURE.md](../ARCHITECTURE.md) / [PLAN.md](../PLAN.md) / [STANDARDS.md](../STANDARDS.md) 为准；学习路径把同一套代码 **拆成可消化的章节与阶梯 Sample**。  
动手实现从 [GETTING_STARTED_M1.md](../GETTING_STARTED_M1.md) 开始；文档总览见 [../README.md](../README.md)。

## 双轨模型

| 轨道 | 目标 | 入口 |
|---|---|---|
| **产品轨** | 可交付的通用 2D·3D 引擎 + Sandbox | `PLAN.md` M1–M25 |
| **学习轨** | 由浅入深理解「如何造引擎」 | 本目录 |

两条轨道共用同一仓库；学习默认走 **D3D12**，M17+ 可对照 **Vulkan**（Linux 见 M18；网络见 M19）。学习轨通过：

1. **阶梯 Sample**（`samples/learn/NN_*`）  
2. **章节文档**（原理 + 代码地图 + 练习）  
3. **ADR**（关键架构决策）  
4. **教学开关**（慢路径、校验、强制同步等）  
5. **术语表 / 数学与着色器速查**  

把复杂度降到可学范围。高级能力（DLSS、DXR、Lightmap、GPU 粒子等）标为 **选修章**。

## 文档索引

| 文档 | 内容 |
|---|---|
| [PATH.md](PATH.md) | 学习路径大纲（必修 / 选修，对齐里程碑） |
| [SAMPLES.md](SAMPLES.md) | 阶梯 Sample 规范与目录约定 |
| [ADR_INDEX.md](ADR_INDEX.md) | 架构决策记录索引（ADR） |
| [GLOSSARY.md](GLOSSARY.md) | 术语表 |
| [BASICS.md](BASICS.md) | 坐标系 / 数学 / HLSL 速查 |
| [DEBUG_WORKFLOW.md](DEBUG_WORKFLOW.md) | PIX/RenderDoc 抓帧（学习向） |
| [../STANDARDS.md](../STANDARDS.md) | **编码 / 架构 / 模块通讯等工程规范** |
| [../TOOLING.md](../TOOLING.md) | **离线工具链**（shader/IBL/cook；ADR 0025） |
| [../GETTING_STARTED_M1.md](../GETTING_STARTED_M1.md) | **M1 可执行清单** |
| [../TESTING.md](../TESTING.md) | **单测 / 集成 / 自动化测试** |
| [../DEBUG_TUNE_TROUBLESHOOT.md](../DEBUG_TUNE_TROUBLESHOOT.md) | 调试 / 调优 / 排错方法 |

章节正文随实现进度逐步补齐（`chapters/CHXX_*.md`），本阶段先定 **封装结构与路径**。

## 代码侧教学挂钩（实现约定）

实现引擎时预留以下能力（配置或控制台），供学习轨使用：

| 开关（示例名） | 作用 |
|---|---|
| `learn.force_sync_gpu` | 每帧等待 GPU，便于单步理解 in-flight |
| `learn.validate_states` | 加强资源状态/描述符校验与日志 |
| `learn.simple_barriers` | 使用更保守的屏障策略（更慢但更易推理） |
| `learn.disable_async_load` | 强制同步加载，先弄清资源生命周期 |
| `learn.show_pass_names` | 叠加 Pass 名称 / 级联染色等教学视图 |

产品默认关闭；Sandbox / learn sample 可按章节打开。

## 与产品目录的关系

```text
samples/
  Sandbox/           # 产品验收：全能小场景
  learn/
    01_clear/
    02_triangle/
    ...              # 见 SAMPLES.md

docs/
  learn/             # 本封装
  ARCHITECTURE.md
  PLAN.md
  POSITIONING.md

engine/              # 产品实现（教学注释在关键路径加强）
```

## 使用方式（建议）

1. 读 [GLOSSARY.md](GLOSSARY.md) + [BASICS.md](BASICS.md)（可并行）  
2. 按 [PATH.md](PATH.md) 必修章顺序做 Sample  
3. 每章：跑通 → 读章节文 → 改练习题 → （可选）PIX 抓帧  
4. 完成必修后再进选修（FG 深化、CSM、蒙皮、超分、DXR…）  
5. 最后用 `Sandbox` 看「拼起来的完整引擎」  

## 相关文档

- [../ARCHITECTURE.md](../ARCHITECTURE.md)  
- [../PLAN.md](../PLAN.md)  
- [../POSITIONING.md](../POSITIONING.md)  
