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
| 玩法 | 不在引擎内 | 同左；工作区「游戏可用」主缺口是 **`game_kit` GK0–GK3**（见 [PLAN.md](PLAN.md) **§1.9**），不是再堆引擎 Pass |

## 2. 缺口 ↔ 里程碑（已排期）

| ID | 缺口 | 状态 | 里程碑 |
|---|---|---|---|
| G01 | 跨后端（VK） | **Win 双后端 100% 收口**（见 [VULKAN_PARITY.md](VULKAN_PARITY.md)）：全栈 post、GPU 实例/Cull/Indirect、探针/IBL 分槽；Linux 仍外置 | M17 |
| G18 | Mesh Shader / GPU Driven | **Cull/Indirect 两端可用**；Bindless 热路径 Feature `bindless_hot_path`（默认 OFF 保黄金图）；VK bindless SKIP | M24 |
| G19 | Linux | **文档+构建说明加深**（[LINUX.md](LINUX.md)、`ENGINE_LINUX_VK`）；X11 窗口/运行时冒烟视 CI | M18 |
| G02–G04、G11 | 混合打磨 / 拣选 / 多 DPI | **可用加深** | M20 |
| G05–G10、G12 | 2D 深度 | **已加深**：chunk→Sprite 展开；SkeletonClip2D；雾 tint / BMFont JSON / 震屏 | M21 |
| G14 | 动态 GI | **已加深**：ProbeVolume + Lightmap 共存；W6 `RefineDensity` 加密网格（非 DDGI）；见 [gi/README.md](gi/README.md)、[ADR 0033](learn/adr/0033-m27-w6-scene-scale.md) | M22 / W6 |
| G15 | 地形/水体/植被（基础） | **可用加深** | M23 |
| G16 | 光追 API 对齐 | **完成（加深）** Feature 门控；W7 `TryBuildCubeBlasTlasAndDispatchRays` 真 BLAS/TLAS+DispatchRays | M25 / W7 |
| T01 | 最小工具链（shader/IBL/纹理/cook/黄金图） | 已排期 | M2–M9；见 [TOOLING.md](TOOLING.md) |
| T03 | 自动化测试加深（准/广） | **Q1–Q3 + C1–C3 + C5–C7 + Q5 ROI 已落地**；C4 双后端比图（默认 ROI + 松闸≈90 PASS，现 RMSE≈74；`--strict` / `-StrictParity` 紧闸 48 可选严） | [PLAN.md](PLAN.md) **§3.1**；不扩 MCP/Harness 命令 |
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
| C01 | 产品级 Deferred / Forward+ 路径钉死 | **已落地（M26）**：Forward+ 钉死 + Pass 名冻结；见 [FORWARD_PLUS.md](FORWARD_PLUS.md)、[ADR 0032](learn/adr/0032-m26-forward-plus-cluster.md) | 高（影响扩展方式） |
| C02 | 集群 / 分块多灯光 | **部分落地（M26/W4）**：CPU≤16 / FrameCB≤16 / Atlas 阴影≤2；CPU `AssignLightsToTiles`（8×4 Forward+ 列表，非完整 GPU 集群） | 高 |
| C03 | IES / Light Function | **部分落地（W7）**：`EvalIesFactor` / `SampleIesLut` + lit `g_local_ies`；非完整 IES 文件生态 | 中 |
| C04 | 更细电影级镜头后处理 | **部分落地（M26/W7）**：vignette + film grain + `chromatic_aberration`（默认 0） | 低 |
| C05 | 大气 / 体积云 / 天气降水 | **部分落地（W4/W7）**：`EvalSkyColor` + `EvalCloudBand` / `CoupleFogWithAtmosphere`；F1 大气/云带；完整天气后置 | 中 |

### 4.2 几何与开放世界加深

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| C06 | Virtual Texture 产品化 | 与「无 VT 全家桶」缺陷对应的加深项 | 中（成本高） |
| C07 | HLOD / Impostor | 超大场景层级；现有仅 LOD/实例 | 中 |
| C08 | Meshlet / 更完整 GPU 几何管线 | **部分落地（M26/W6）**：`Path::MeshShader` + `MeshletPathAvailable`（Feature `meshlet`）门控 SKIP；Indirect Cull 保留 | 中 |
| C09 | FFT / 高级水面 | **部分落地（W6）**：`AnimateWaterPatch` Gerstner 式高度/法线；完整 FFT 海洋后置 | 低 |

### 4.3 动画与角色

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| C10 | 动画混合树 / 状态机 | **部分落地（M26/W6）**：`AnimationStateMachine` + `SampleBlend` 多 clip 权重；完整混合树图后置 | 高（上层常自建） |
| C11 | IK | 足部/瞄准等 | 中 |
| C12 | GPU 蒙皮产品化打磨 | **部分落地（W7）**：`GpuSkinningAvailable` + `SkinVerticesGpuDispatch`（Feature `gpu_skinning`）→ D3D12 `skin_cs.hlsl` / `TryDispatchGpuSkinD3d12`；失败回退 CPU stub；VK SKIP | 低 |

### 4.3b 物理加深

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| **C22** | **薄 SoftBody / Cloth** | **已落地**：`IPhysicsWorld` + Jolt + Sandbox DebugDraw；builtin SKIP；[ADR 0029](learn/adr/0029-physics-softbody-boundary.md) | 已收口（看板 W-phys-soft） |

### 4.4 2D / 文本 / 矢量

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| **G13** | **矢量 / 路径绘制** | SVG 级；原「可选后置」 | 按产品需要 |
| C13 | 九宫格 / 更完整 2D UI 精灵约定 | 偏运行时 UI 与 2D 共用 | 低 |
| C14 | 3D 世界文字 | **部分落地（W7）**：`BuildWorldTextBillboards`（BMFont 广告牌）；Sandbox DebugDraw 线框 | 中 |
| C15 | 2D 富文本 / 复杂排版 | 超出 BMFont 基础 | 低 |

### 4.5 运行时工程

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| C16 | 资源热更 / 着色器热重载 | **部分落地（M26/W6）**：`ShaderHotReload::Poll` + `NeedsPsoRebuild` / `ConsumePsoRebuildRequest`；宿主负责重建 PSO | 中 |
| C17 | 多窗口 / 多 GPU | 特殊部署 | 低 |
| C18 | 立体 / XR 渲染 | 输入层可预留适配器；渲染未排期 | 低（易扩范围） |

### 4.6 宿主 · 脚本 · 编辑器（引擎外或可选）

> 细则：[HOSTING.md](HOSTING.md)、ADR 0027。默认 **不进** M1–M25。

| ID | 候选 | 说明 | 优先级建议 |
|---|---|---|---|
| **C19** | `IScriptHost` 抽象（默认可空） | 见 [game_kit/docs](../../game_kit/docs/README.md)；VM 在外层 | 中 |
| **C20** | 轻量内容工具 | **部分落地（M26）**：`tools/content_lint` 校验 manifest + 打印依赖；场景图/视口编辑器仍外置 | 中 |
| **C21** | 独立 `editor/` 视口编辑器 | 工作区独立工程；**规格/排期不在本目录**（见 [LAYERS](../../docs/LAYERS.md)） | 低～中 |

### 4.7 立项规则

1. 单项进入计划前必须：**验收标准 + Feature/L0–L2 归属 + 双后端策略**（宿主类可标「引擎外」）。  
2. 默认仍遵守：不做 mac/移动、不做完整编辑器进 `engine/`、不做玩法/同步进引擎、不做音频 DSP。  
3. C01/C02/C10/G13 若要做，建议优先于 C06/C18。  
4. 脚本/编辑器优先走 **HOSTING 外挂**；C19–C21 立项不得默认同步改 POSITIONING。**Win 双后端 100%**（含薄 SoftBody）已按 [VULKAN_PARITY.md](VULKAN_PARITY.md) 收口；下一批优先 Linux/大气等口径外项。  
5. **服装级**布料编辑/穿戴/撕裂资产流 **不进** `engine/`（与 C22 区分）。

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
| — | **完整**载具轮胎 / 破坏专用求解器产品化（薄 SoftBody/Cloth 见 **C22**） |
| — | 服装级布料编辑、穿戴绑定、撕裂内容管线（引擎外） |

## 6. 相关文档

- [../../docs/LAYERS.md](../../docs/LAYERS.md) — **工作区分层权威**（独立 `editor/` 规格不在本目录）  
- [HOSTING.md](HOSTING.md) — **玩法层 / 脚本如何外挂**  
- [../../game_kit/docs/README.md](../../game_kit/docs/README.md) — **通用玩法壳 + 脚本规格**  
- [../../genre_kits/README.md](../../genre_kits/README.md) · [../../games/README.md](../../games/README.md) — 品类层 / 游戏工程  
- [PLAN.md](PLAN.md) §1.7 / §1.8 / **§1.9 游戏可用水位** / §4  
- [TOOLING.md](TOOLING.md)  
- [POSITIONING.md](POSITIONING.md)  
- [learn/adr/0023-engine-gap-fill-m20-m25.md](learn/adr/0023-engine-gap-fill-m20-m25.md)  
- [learn/adr/0025-toolchain-minimum-viable.md](learn/adr/0025-toolchain-minimum-viable.md)  
- [learn/adr/0026-runtime-foundations-assets-threads-profiling.md](learn/adr/0026-runtime-foundations-assets-threads-profiling.md)  
- [learn/adr/0027-hosting-script-editor-boundary.md](learn/adr/0027-hosting-script-editor-boundary.md)  
- [learn/adr/0028-genre-kits-layering.md](learn/adr/0028-genre-kits-layering.md)  
- [learn/adr/0029-physics-softbody-boundary.md](learn/adr/0029-physics-softbody-boundary.md)  
- [README.md](README.md)  
