# ADR 0025: 最小可行工具链；引擎内不做可视化内容编辑器

- 状态: Accepted
- 日期: 2026-08-12
- 关联: PLAN §1.8, docs/TOOLING.md, tools/；修订对齐 ADR 0027

## 背景

通用渲染引擎若无离线烘焙与着色器编译路径，PBR/IBL/发版无法复现；若把完整可视化编辑器做进 `engine/` 则范围爆炸。需明确「必要工具」与「引擎内不做」。

## 决策

1. 提供最小工具链：shader_compile（**M2 强制**）、ibl_baker、lightmap_baker、纹理压缩路径、最小 asset cook/清单（**含依赖图与可选打包**，见 RUNTIME_FOUNDATIONS）、黄金图脚本；2D 图集**约定** + Tiled 导入。  
2. **`render_engine` 内不做**完整关卡/材质节点/UI 可视化编辑器；默认内容路径 = 外部 DCC + 本仓库 CLI。  
3. **允许**工作区独立工程 [`editor/`](../../../editor/)（C21）与轻量 CLI（C20）；不以本 ADR 禁止外挂编辑器（见 ADR 0027 / HOSTING）。  
4. 主网格交换格式：**glTF**；FBX/USD 不作为一期必做。  
5. 里程碑绑定见 [TOOLING.md](../../TOOLING.md) 与 PLAN §1.8。  
6. 工具可依赖构建机三方（DXC、DirectXTex）；运行时仍守抽象层（ADR 0017）。

## 后果

- 优点：内容可复现、引擎范围可控；外挂编辑器可另仓演进。  
- 代价：开箱无引擎内嵌点选式编辑；需 DCC/CLI 或独立 `editor/`。

## 学习提示

1. baker 的输入输出格式要写成契约，否则 Sample 无法复现。  
2. cook 清单是发版与流式的枢纽，不是可有可无的脚本。  
3. 「不做编辑器」= 不做进 **engine 核心**，不是工作区禁止 `editor/`。  
