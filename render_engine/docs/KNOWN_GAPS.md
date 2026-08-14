# 已知缺口与里程碑映射（引擎向）

> 对应 [POSITIONING.md](POSITIONING.md)。  
> **图形后端仅 D3D12 + Vulkan**；**macOS / 移动 / Metal 明确不做**。  
> **M20–M25** 补齐引擎向渲染/2D/场景短板；§3 为做完后仍弱项；§4 为 **M25 后候选**（未排期、非「明确不做」）；§5 为范围外。

## 1. 定稿后能力水位

| 维度 | M1–M19 | + M20–M25 |
|---|---|---|
| 平台 | Win（D3D12+VK）+ Linux（VK） | 同左（**无** mac/移动） |
| 3D | P0/P1 + 简化 GI | + 动态 GI；地形/水体/植被基础；GPU Driven；VK RT |
| 2D | 像素混合基础（M16） | + 阴影/MV/分层后处理/拣选；流式；骨骼；光雾等 |
| 网络 | HTTP/WS/QUIC 传输 | 同左 |
| 玩法 | 不在引擎内 | 同左 |

## 2. 缺口 ↔ 里程碑（已排期）

| ID | 缺口 | 状态 | 里程碑 |
|---|---|---|---|
| G01 | 跨后端（VK） | **对标加深中**（Win lit/CSM；IBL/post 本档） | M17 |
| G19 | Linux | **文档占位 / 外置** | M18 |
| G02–G04、G11 | 混合打磨 / 拣选 / 多 DPI | **可用加深** | M20 |
| G05–G10、G12 | 2D 深度 | **可用加深** | M21 |
| G14 | 动态 GI | **可用加深**（ProbeVolume） | M22 |
| G15 | 地形/水体/植被（基础） | **可用加深** | M23 |
| G18 | Mesh Shader / GPU Driven | **加深中**（Indirect 本档） | M24 |
| G16 | 光追 API 对齐 | **完成（加深）** Feature 门控 | M25（内容管线仍非 UE 级） |
| T01 | 最小工具链（shader/IBL/纹理/cook/黄金图） | 已排期 | M2–M9；见 [TOOLING.md](TOOLING.md) |
| T02 | 图集约定 + Tiled 导入 | 已排期 | M16 |
| R01 | Cook 依赖图 / 异步回调 / Handle 寿命 / **数据依赖与生命周期** / 逻辑渲染分离 / GPU Profiling | 已排期 | M1–M14；见 [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md) |

## 3. 补齐 M25 后仍保留的引擎缺陷（摘要）

详见 [POSITIONING.md](POSITIONING.md) §2：

- **引擎内**无编辑器与脚本（外挂见 [HOSTING.md](HOSTING.md)、[LAYERS](../../docs/LAYERS.md)、`game_kit/` / `genre_kits/` / `games/` / `editor/`）；工具侧仅最小 CLI + 可选 C20  
- 开放世界无 VT / Nanite 级；地形水植被仅基础  
- 动态 GI / 光追深度不及顶尖产品  
- 音频/物理/网络能力边界；非 ECS；双后端与硬解视频约束；无资产生态  

这些是**产品水位**，不是「忘了排期」。

## 4. M25 后候选（未排期，非明确不做）

> 相对更完整的「通用渲染中台」仍可能缺、**当前不进 M1–M25**。  
> 若要立项：先写 ADR + 改 PLAN，再开里程碑（建议簇名 **P3 / M26+**）。  
> **不要**与 §5 范围外混淆。

### 4.1 渲染与光照

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| C01 | 产品级 Deferred / Forward+ 路径钉死 | 现以通用 Pass/FrameGraph 为主，未强制选路径 | 高（影响扩展方式） |
| C02 | 集群 / 分块多灯光 | 大量点/聚光的提交与剔除策略 | 高 |
| C03 | IES / Light Function | 灯光分布与投影函数 | 中 |
| C04 | 更细电影级镜头后处理 | 色散、胶片颗粒、暗角等（基础栈已在 P1） | 低 |
| C05 | 大气 / 体积云 / 天气降水 | 超出「体积雾」的环境表现 | 中 |

### 4.2 几何与开放世界加深

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| C06 | Virtual Texture 产品化 | 与「无 VT 全家桶」缺陷对应的加深项 | 中（成本高） |
| C07 | HLOD / Impostor | 超大场景层级；现有仅 LOD/实例 | 中 |
| C08 | Meshlet / 更完整 GPU 几何管线 | 在 M24 GPU Driven 之上加深 | 中 |
| C09 | FFT / 高级水面 | M23 水体仅为基础 | 低 |

### 4.3 动画与角色

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| C10 | 动画混合树 / 状态机 | 现有片段+蒙皮+Morph，缺图逻辑 | 高（上层常自建） |
| C11 | IK | 足部/瞄准等 | 中 |
| C12 | GPU 蒙皮产品化打磨 | 蒙皮已有；算力路径可加深 | 低 |

### 4.4 2D / 文本 / 矢量

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| **G13** | **矢量 / 路径绘制** | SVG 级；原「可选后置」 | 按产品需要 |
| C13 | 九宫格 / 更完整 2D UI 精灵约定 | 偏运行时 UI 与 2D 共用 | 低 |
| C14 | 3D 世界文字 | P2 主要是 2D BMFont | 中 |
| C15 | 2D 富文本 / 复杂排版 | 超出 BMFont 基础 | 低 |

### 4.5 运行时工程

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| C16 | 资源热更 / 着色器热重载 | 迭代体验 | 中 |
| C17 | 多窗口 / 多 GPU | 特殊部署 | 低 |
| C18 | 立体 / XR 渲染 | 输入层可预留适配器；渲染未排期 | 低（易扩范围） |

### 4.6 宿主 · 脚本 · 编辑器（引擎外或可选）

> 细则：[HOSTING.md](HOSTING.md)、ADR 0027。默认 **不进** M1–M25。

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| **C19** | `IScriptHost` 抽象（默认可空） | 见 [game_kit/docs](../../game_kit/docs/README.md)；VM 在外层 | 中 |
| **C20** | 轻量内容工具 | Manifest 浏览等；可先于 [editor](../../editor/docs/README.md) | 中 |
| **C21** | 独立 `editor/` 视口编辑器 | 规格见 [editor/docs](../../editor/docs/README.md) | 低～中 |

### 4.7 立项规则

1. 单项进入计划前必须：**验收标准 + Feature/L0–L2 归属 + 双后端策略**（宿主类可标「引擎外」）。  
2. 默认仍遵守：不做 mac/移动、不做完整编辑器进 `engine/`、不做玩法/同步进引擎、不做音频 DSP。  
3. C01/C02/C10/G13 若要做，建议优先于 C06/C18。  
4. 脚本/编辑器优先走 **HOSTING 外挂**；C19–C21 立项不得默认同步改 POSITIONING。

## 5. 明确范围外（不做）

| ID | 项 |
|---|---|
| G20 | 移动端（iOS/Android 等） |
| — | macOS / Metal |
| — | D3D11 / OpenGL / GLES **实装** |
| — | 玩法、**引擎内**脚本 VM、状态同步、匹配/反作弊、NavMesh 产品（外挂见 HOSTING / LAYERS；品类 → genre_kits） |
| G17 / T03 | **引擎内**材质节点图 / 完整可视化编辑器；FBX·USD 一站式（独立 editor 见 C21） |
| — | 音频特效（DSP / 完整空间音频） |
| — | Frame Generation；商业资产生态 |
| — | 布料/软体/载具轮胎等物理产品化 |

## 6. 相关文档

- [../../docs/LAYERS.md](../../docs/LAYERS.md) — **工作区分层权威**  
- [HOSTING.md](HOSTING.md) — **玩法层 / 脚本 / 编辑器如何外挂**  
- [../../game_kit/docs/README.md](../../game_kit/docs/README.md) — **通用玩法壳 + 脚本规格**  
- [../../genre_kits/README.md](../../genre_kits/README.md) · [../../games/README.md](../../games/README.md) — 品类层 / 游戏工程  
- [../../editor/docs/README.md](../../editor/docs/README.md) — **编辑器规格**  
- [PLAN.md](PLAN.md) §1.7 / §1.8 / §4  
- [TOOLING.md](TOOLING.md)  
- [POSITIONING.md](POSITIONING.md)  
- [learn/adr/0023-engine-gap-fill-m20-m25.md](learn/adr/0023-engine-gap-fill-m20-m25.md)  
- [learn/adr/0025-toolchain-minimum-viable.md](learn/adr/0025-toolchain-minimum-viable.md)  
- [learn/adr/0026-runtime-foundations-assets-threads-profiling.md](learn/adr/0026-runtime-foundations-assets-threads-profiling.md)  
- [learn/adr/0027-hosting-script-editor-boundary.md](learn/adr/0027-hosting-script-editor-boundary.md)  
- [learn/adr/0028-genre-kits-layering.md](learn/adr/0028-genre-kits-layering.md)  
- [README.md](README.md)  
