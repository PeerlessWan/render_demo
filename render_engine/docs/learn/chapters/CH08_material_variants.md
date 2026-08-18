# CH08 — 材质变体

## 目标

学完应能：Keyword→PSO 与卡顿来源。

## 前提

CH07。

## 原理

材质实例。

建议先跑通 sample，再对照引擎关键路径阅读，避免只背名词。

## 代码地图

| 位置 | 说明 |
|---|---|
| `samples/learn/08_material_variants/` | 本章阶梯 Sample |
| `engine/app/application.*` | 主循环与模块挂载 |
| `docs/learn/PATH.md` | 章序与「你应能回答」 |
| `docs/GETTING_STARTED_M1.md` | 编译运行清单 |

结合 sample README 的「代码地图」表下钻到具体符号。

## 建议断点 / PIX 看什么

- 主循环 `Application::Run` 一帧边界。
- Present / 提交前的资源状态（若本章涉及 GPU）。
- 打开 `learn.show_pass_names` / `learn.force_sync_gpu` 便于单步（见 learn README 教学开关）。

## 练习

1. **必做**：按 sample README「怎么跑」编译运行，并回答 PATH「你应能回答」中的问题。  
2. **必做**：完成 sample README「必做练习」至少一题。  
3. **选做**：用 PIX/RenderDoc 抓一帧，对照本章原理。

## 常见坑

- 跳过 sample 直接读大 ARCHITECTURE，容易迷失。  
- 教学开关未开，却按产品默认路径硬猜。  
- README 与代码不同步时以代码与 Feature/Status 为准。

## 延伸阅读

- [PATH.md](../PATH.md) · [SAMPLES.md](../SAMPLES.md) · [GLOSSARY.md](../GLOSSARY.md) · [BASICS.md](../BASICS.md)
