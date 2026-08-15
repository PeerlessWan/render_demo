# 宿主接入：玩法层 · 脚本 · 编辑器

> 本引擎（`render_engine`）是 **渲染中台**。玩法、脚本 VM、完整可视化编辑器 **默认不在引擎内实现**。  
> 本文规定：**外面怎么挂**、引擎保证什么 API 面、禁止碰什么。  
> 相关：[POSITIONING.md](POSITIONING.md)、[KNOWN_GAPS.md](KNOWN_GAPS.md) §4/§5、[RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md)、[../../docs/LAYERS.md](../../docs/LAYERS.md)、ADR 0017 / 0019 / 0025 / **0027** / **0028**。

## 1. 推荐仓库关系

> 工作区分层权威：[../../docs/LAYERS.md](../../docs/LAYERS.md)（**薄 game_kit + 可选 genre_kits + games**，ADR 0028）。

```text
render_demo/                    # 工作区（可 mono 可多仓）
├── tools/                      # 离线工具过渡位 → 目标 render_engine/tools
├── render_engine/              # 本引擎：渲染 + 物理/UI/传输等
│   ├── docs/                   # HOSTING / HOST_API / PREFAB_SCHEMA …
│   └── engine/ …
├── game_kit/                   # 品类无关：玩法壳 + 脚本（game_kit/docs）
│   └── 依赖 render_engine 公开头
├── genre_kits/                 # 可选品类层（rpg_kit / shooter_kit …）
│   └── 依赖 game_kit；不依赖具体 games/*
├── games/                      # 具体游戏工程（内容 / 数值 / 关卡）
│   └── 可选依赖 0..N 个 genre kit
└── editor/                     # 独立编辑器（文档：editor/docs）
    └── 视口链 render_engine；工具 UI 自管
```

详细规格：

- 分层总览：[../../docs/LAYERS.md](../../docs/LAYERS.md)  
- 脚本 / 通用玩法：[../../game_kit/docs/README.md](../../game_kit/docs/README.md)  
- 品类 kit：[../../genre_kits/README.md](../../genre_kits/README.md)  
- 游戏工程：[../../games/README.md](../../games/README.md)  
- 视口编辑器：**不在本目录**；工作区独立 `editor/`（经 LAYERS）  

命名约定：对外可称「游戏引擎 = game_kit + render_engine」；品类能力来自可选 `genre_kits/*`；**本仓 `render_engine` 文档仍只描述渲染中台**。

---

## 2. 引擎向宿主提供的能力（契约面）

实现稳定后，上层应只依赖公开头（路径以实现为准，如 `include/engine/...`）。

| 域 | 宿主可用 | 说明 |
|---|---|---|
| 主循环 | `Application` / `IModule` | 玩法以 Module 挂入，或嵌入时按文档泵帧 |
| 场景 | Node 树、相机、序列化（渲染向） | 不做玩法存档语义 |
| 资源 | `AssetId` / `AssetHandle`、异步 `Pump`、依赖图 | 见 RUNTIME_FOUNDATIONS |
| 输入 | ActionMap / 轴与按钮 | 禁止散落 VK；尊重 UI WantCapture |
| 物理 | 查询、触发器、角色控制器、DebugDraw | 规则在玩法层 |
| UI | Retained HUD/菜单 + ImGui 调试（引擎自用） | 玩法 UI 走 Retained 或上层自绘 |
| 音频 | 解码 + 输出 + 增益 | 无 DSP；特效在上层中间件 |
| 网络 | HTTP / WS / QUIC 传输 + Pump | 无状态同步 |
| 2D | Sprite / Tilemap 渲染与碰撞数据导出 | 无 RPG 逻辑 |
| 调试 | Profiler、控制台、Feature 查询 | 编辑器视口可复用 |

**明确不提供：** 脚本语言运行时、任务/对话/背包、NavMesh 产品、匹配/复制、材质节点图编辑器。

---

## 3. 禁止依赖

上层（`game_kit` / `genre_kits/*` / `games/*` / `editor` / Sample 业务 Module）**不得**：

1. `#include` `backends/**`、三方头（ADR 0017）  
2. 持有/缓存裸 `ID3D12*` / `Vk*`；只用引擎 Handle  
3. 在 IO / 脚本线程直接改 Scene 权威树或创建 GPU 资源（须经主线程相位）  
4. 假设与引擎同进程的「私有符号」长期稳定  

---

## 4. 帧相位（玩法 / 脚本插入点）

与 Application 主循环对齐（名称以实现为准）：

```text
Platform.PumpEvents
Input.PollAndDispatch
Net.Pump
Asset.PumpAsync          # 加载完成回调仅此之后；先于玩法
Module.OnUpdate          # ← 玩法 / 脚本 Tick 默认插这里
Video / Audio Tick（若启用）
Physics.Step → World/Animation
RenderScene 抽取
RenderSystem.Frame
```

> 与 [ARCHITECTURE.md](ARCHITECTURE.md) §4.1 同一权威顺序。  
> **编辑器变体：** 在 `Module.OnUpdate` 之前或之中跑 EditorUI；非 Play 时跳过 game_kit；Play 时复用上表相位。循环细节在独立 `editor/` 工程，**不在本目录**。

约束：

- 脚本/玩法 **默认主线程**；重计算可自建线程，回写 Scene 必须入队到 Update。  
- 退出：玩法先停 → 取消异步 → `Stopping` → `GPUDrained` → Shutdown（RUNTIME_FOUNDATIONS §7）。

---

## 5. 脚本怎么加

### 5.1 推荐：外层自带 VM（方案 A）

1. 在 `game_kit`（或游戏工程）引入 Lua / Wren / Angelscript 等。  
2. 绑定仅封装 **§2 契约面**；错误映射为可日志的失败。  
3. 热重载、调试器、沙箱策略由玩法层负责。  
4. 本引擎 **零脚本依赖**。

### 5.2 可选：引擎仅留宿主抽象（方案 B，C19）

- 增加 `IScriptHost`（注册函数、`Tick`、报错回调），**默认空实现**。  
- 具体 VM 放 `plugins/script_lua` 或 `game_kit`。  
- 须 ADR + 不进入 M1–M25 主验收。

### 5.3 不推荐：引擎内置 VM（方案 C）

仅当 POSITIONING 改为「轻量游戏引擎」并开 M26+ 时；见 KNOWN_GAPS 立项规则。

### 5.4 绑定白名单（建议）

允许：Node 变换、生成/销毁（经场景 API）、播动画片段、播 AudioClip、UI 显隐/文本、物理 Raycast、启动异步加载、读 Action、发玩法事件。  
禁止：创建 Device、改 PSO、直写描述符、绕过 Handle 释放 GPU 资源。

---

## 6. 编辑器怎么加

### 6.1 默认路径（已定）

外部 DCC + `tools/` CLI（cook / baker）+ 场景序列化。见 [TOOLING.md](TOOLING.md)、ADR 0025。

### 6.2 轻量工具（C20，可选）

- AssetId / Manifest 浏览器、Prefab/场景 JSON 校验、脚本热重载辅助。  
- 形态：CLI 或极简 ImGui 小工具；**不是**关卡视口编辑器。  
- 可放 `render_engine/tools/` 或 `game_kit/tools/`。

### 6.3 独立编辑器工程（C21，可选、更晚）

```text
editor/
  ├── 视口：创建 Application 或嵌入 RHI 表面
  ├── 工具 UI：ImGui（或自研），与游戏 Retained UI 分离
  ├── 改场景 → 写序列化 / Manifest；保存走 cook 依赖规则
  └── 与 runtime 可同进程调试，发版可分离
```

约束：

1. 编辑器 **不**链入 `engine` 内部编译单元改热路径。  
2. 与 runtime 共享资产格式与依赖图。  
3. 完整材质节点图 / UMG 级 UI 编辑器仍属 §5 范围外，除非新 ADR 改定位。  
4. 单独里程碑与验收（不阻塞 M1–M25）。

---

## 7. Sandbox 分工

| 工程 | 验收什么 |
|---|---|
| `render_engine` / Sandbox | 渲染、物理演示、UI 壳、媒体、网络传输 |
| `game_kit` Demo | 品类无关可玩切片（移动、交互、脚本驱动） |
| `genre_kits/<name>_kit` Demo | 该品类最小可复用切片（可选） |
| `games/<title>` | 内容完整度与该作验收 |
| `editor` | 选中、改属性、存盘、再进 runtime 可加载 |

---

## 8. 文档与立项清单

| 若要做 | 动作 |
|---|---|
| 仅外挂通用玩法+脚本 | 建/推进 `game_kit`；遵守本文与 [LAYERS](../../docs/LAYERS.md)；引擎补公开 API 稳定性 |
| 某品类可复用玩法 | 在 `genre_kits/<name>_kit` 建仓；**勿**加厚进 `game_kit`（ADR 0028） |
| 具体游戏 | `games/<title>`；可先内聚，再抽 kit |
| 跨品类中间件（同步/寻路/空间音频等） | 与 genre kit 平级另立；默认不进引擎核心 |
| `IScriptHost` | KNOWN_GAPS **C19** + ADR |
| 轻量内容工具 | **C20** |
| 独立视口编辑器 | **C21** + 编辑器专用 ADR |
| 引擎内置脚本/完整编辑器 | 先改 POSITIONING，再开 P3 里程碑 |

---

## 9. 相关文档

- [../../docs/LAYERS.md](../../docs/LAYERS.md) — **工作区分层权威**  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [HOST_API.md](HOST_API.md)  
- [PREFAB_SCHEMA.md](PREFAB_SCHEMA.md)  
- [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md)  
- [TOOLING.md](TOOLING.md)  
- [STANDARDS.md](STANDARDS.md)  
- [PLAN.md](PLAN.md)  
- [../../game_kit/docs/README.md](../../game_kit/docs/README.md)  
- [../../genre_kits/README.md](../../genre_kits/README.md)  
- [../../games/README.md](../../games/README.md)  
- [learn/adr/0027-hosting-script-editor-boundary.md](learn/adr/0027-hosting-script-editor-boundary.md)  
- [learn/adr/0028-genre-kits-layering.md](learn/adr/0028-genre-kits-layering.md)  
