# 架构设计

> 产品：**Windows/Linux 通用 2D·3D 渲染引擎**（仅 D3D12 + Vulkan）  
> 状态：定稿架构（[PLAN.md](PLAN.md)：M1–M19 + **M20–M25 缺口补齐**）  
> 学习封装：[learn/README.md](learn/README.md)

## 1. 目标与原则

### 1.1 目标

- 引擎接管主循环，业务通过 `Application` / `Module` 扩展。
- 渲染：RHI + FrameGraph；**仅 D3D12（Windows）与 Vulkan（Windows+Linux）**；D3D11/GL/GLES 仅 stub；**不做** macOS/移动/Metal。
- 通用 3D（P0/P1 + **P2 缺口补齐**）+ 2D/像素混合 + 物理 + UI + 媒体 + 网络传输。
- 平台：Windows Win32；Linux X11（Wayland 目标内）。**明确不做** macOS/移动。
- 教学封装与外设接入层保留。

### 1.2 设计原则

| 原则 | 说明 |
|---|---|
| 分层解耦 | 业务不直接依赖 D3D12 头文件；只依赖引擎公开 API |
| 后端可替换 | RHI 抽象稳定；工厂注册后端；未实装后端返回明确错误 |
| 扩展优先 Pass/模块 | 自定义渲染优先加 FrameGraph Pass、Material 变体或 Module |
| 可降级 | 无 DXR 时走光栅阴影；超分无 NGX/FFX → `builtin_bilinear`（ADR 0044 可选 SDK） |
| 可读可维护 | 命名清晰、模块边界清楚、关键约定文档化 |
| 可学习 | 关键路径可加强注释；`learn.*` 开关提供慢路径/校验；文档双轨更新 |
| 外设可扩展 | 业务只依赖 `IInputDevice` / ActionMap；具体 HID/XInput/WinRT 藏在 adapters |
| **三方可替换** | **凡三方库必须经抽象/适配层**；业务不直链三方头；换实现不改上层调用（见 THIRD_PARTY / ADR 0017） |

工程细则（编码、模块通讯、线程、错误模型等）见 **[STANDARDS.md](STANDARDS.md)**。

## 2. 逻辑分层

```text
┌──────────────────────────────────────────────────────────┐
│  教学封装 Learn Layer（docs/learn + samples/learn）         │
│  路径 / ADR / 术语 / 阶梯 Sample / learn.* 开关说明         │
├──────────────────────────────────────────────────────────┤
│  samples/Sandbox · 产品 Module（继承 Application）           │
└──────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────┐
│  Application / ModuleSystem / Config / Event / Log         │
├──────────┬──────────┬──────────┬──────────┬──────────────┤
│ Platform │ Input    │ Assets   │ Scene    │ Animation    │
│ Window   │ 外设接入层│ VFS/Async│ World    │ Skin/Clip    │
│ DPI/Resize│ Devices │          │ Node     │              │
├──────────┴──────────┴─────┬────┴──────────┴──────────────┤
│  Media：VideoTexture(随后端硬解) · Audio(解码+输出，无特效)   │
│  Net：HTTP(S) · WebSocket · QUIC 可靠流（轻量三方封装）        │
├───────────────────────────┬──────────────────────────────┤
│  Physics（Jolt 等第三方封装）│  RenderSystem / Visibility    │
│  World · Bodies · Queries  │  LOD · Occlusion · Instancing │
├────────────────────────────┼───────────────────────────────┤
│  UI：ImGui(调试) · Retained(HUD/菜单) · Fonts · UI Pass    │
├────────────────────────────┼───────────────────────────────┤
│  Render2D / Pixel：Sprite · Atlas · Tilemap · SortLayers   │
├────────────────────────────┴───────────────────────────────┤
│  Material · VFX · PostProcess(TAA/AO/SSR/…) · Upscaler · RT│
├──────────────────────────────────────────────────────────┤
│  FrameGraph                                                │
├──────────────────────────────────────────────────────────┤
│  RHI（Device/Queue/Cmd/Resource/PSO/Sync/Descriptor）      │
├──────────────────────────────────────────────────────────┤
│  backends/d3d12（Windows）                                  │
│  backends/vulkan（Windows + Linux）                         │
│  backends/d3d11|opengl|gles（stub）                         │
└──────────────────────────────────────────────────────────┘
```

### 2.1 各层职责

| 层 | 职责 | 谁可以改 |
|---|---|---|
| 产品 / Sample | 玩法、场景组装、配置 | 业务方 |
| Application | 启停、帧循环、模块生命周期 | 引擎核心 |
| Platform | Win32；Linux X11/Wayland（不做 macOS/移动） | 引擎核心 |
| Backend | **仅 D3D12、Vulkan**；D3D11/GL/GLES stub | 后端维护者 |
| **Input（外设接入层）** | 外设枚举、热插拔、状态采样、动作映射、适配器 | 引擎核心 + 可选插件 |
| Assets | 加载、缓存、异步+回调、Handle 引用、Manifest/依赖；与 tools cook 协同 | 引擎核心 + tools |
| **Media** | 视频/音频抽象（`IVideoDecoder` 等）+ 随后端/三方实现；无软解视频、无音频特效 | 引擎核心 |
| **Net** | HTTP/WS/QUIC **抽象** + 三方 adapters；主循环 Pump | 引擎核心 |
| **Physics** | `IPhysicsWorld` + 三方 adapters；刚体/触发器/查询/角色控制器 | 引擎核心 |
| **UI** | UI 抽象 + ImGui/RmlUi adapters；字体；输入捕获；UI Pass | 引擎核心 |
| **Render2D / Pixel** | Sprite、图集动画、Tilemap、像素采样/缩放、混合排序 | 引擎核心 |
| Scene / Animation | 节点、组件、LOD、剔除、蒙皮、Morph | 引擎核心 |
| RenderSystem | RenderScene、可见性（视锥+遮挡）、实例化、FrameGraph | 引擎核心 |
| FrameGraph | Pass 声明、依赖、屏障插入点、执行 | 引擎核心 |
| RHI | 后端无关图形抽象 | 引擎核心 |

## 3. 目录结构（目标）

> 工作区分层权威：[../../docs/LAYERS.md](../../docs/LAYERS.md)。

```text
render_demo/                     # 工作区根
├── README.md
├── docs/LAYERS.md               # 分层权威（薄 game_kit + genre_kits + games）
├── tools/                       # 离线工具（过渡：目标迁入 render_engine/tools）
├── game_kit/                    # 品类无关玩法壳 + 脚本（见 game_kit/docs）
├── genre_kits/                  # 可选品类层（rpg_kit / shooter_kit …）
├── games/                       # 具体游戏工程
├── editor/                      # 独立视口编辑器（见 editor/docs）
└── render_engine/               # 渲染引擎本体
    ├── README.md
    ├── CMakeLists.txt           # 目标
    ├── docs/                    # 见 docs/README.md 总索引
    │   ├── README.md
    │   ├── GETTING_STARTED_M1.md
    │   ├── HOSTING.md
    │   ├── HOST_API.md
    │   ├── PREFAB_SCHEMA.md
    │   ├── ARCHITECTURE.md      ← 本文档
    │   ├── PLAN.md
    │   ├── POSITIONING.md
    │   ├── KNOWN_GAPS.md
    │   ├── RUNTIME_FOUNDATIONS.md
    │   ├── TOOLING.md
    │   ├── STANDARDS.md
    │   ├── TESTING.md
    │   ├── THIRD_PARTY.md
    │   ├── DEBUG_TUNE_TROUBLESHOOT.md
    │   └── learn/               # 教学封装 + ADR
    ├── engine/
    │   ├── application/
    │   ├── core/                # Result, Version, Log, Math, Clock, Event, Config
    │   ├── platform/            # win32/, linux/（x11, wayland）
    │   ├── input/               # 外设接入层：Devices, ActionMap, adapters
    │   ├── assets/              # AssetManager, FileSystem, Importers（含静态图）
    │   ├── media/               # IVideoDecoder/IAudio* 抽象 + 后端/三方 adapters
    │   ├── net/                 # IHttpClient/IWebSocket/IQuic + adapters/（三方实现）
    │   ├── physics/             # IPhysicsWorld + adapters/（Jolt 等）
    │   ├── ui/                  # UI 抽象 + ImGui/RmlUi adapters；Fonts；UI Pass
    │   ├── render2d/            # Sprite, Atlas, PixelCamera, SortLayers
    │   ├── tilemap/             # Tilemap 渲染与 Tiled 等格式导入（渲染向）
    │   ├── scene/               # World, Node, LOD, Serialization, Visibility
    │   ├── animation/           # Skeleton, Skinning, AnimationClip
    │   ├── material/            # Material, MaterialInstance, ShaderTemplate, Variants
    │   ├── render/
    │   │   ├── RenderSystem
    │   │   ├── Camera, Environment
    │   │   ├── FrameGraph
    │   │   └── rhi/             # 公开 RHI 接口与类型
    │   ├── vfx/                 # Particle(CPU/GPU), Trail, Decal
    │   ├── post/                # PostProcessStack, IUpscaler + adapters
    │   ├── debug/               # DebugDraw, Views, Console, Profiler, Readback
    │   └── backends/
    │       ├── d3d12/           # Windows
    │       ├── vulkan/          # Windows + Linux
    │       ├── d3d11/           # stub
    │       ├── opengl/          # stub
    │       └── gles/            # stub
    ├── third_party/             # 三方源码/SDK（仅被 adapters 引用）
    ├── plugins/                 # 可选：后端/超分插件挂载点
    ├── tools/                   # 目标位置（现暂在工作区根 tools/）
    │   ├── shader_compile/      # DXC → DXIL/SPIR-V（或 CMake 命令）
    │   ├── ibl_baker/           # IBL 三件套
    │   ├── lightmap_baker/      # 简化 Lightmap / 烘焙 GI
    │   ├── texture_compress/    # BC/DDS 或 KTX（可封装 DirectXTex）
    │   ├── asset_cook/          # 清单 / 可选打包
    │   └── README.md            # 指向 docs/TOOLING.md
    ├── shaders/                 # HLSL 源
    ├── assets/                  # 默认与示例资产
    ├── tests/                   # Catch2 unit / integration / golden；见 TESTING.md
    │   ├── unit/
    │   ├── integration/
    │   └── scripts/
    └── samples/
        ├── Sandbox/             # 产品验收
        └── learn/               # 阶梯 Sample（见 docs/learn/SAMPLES.md）
            ├── 01_clear/
            └── ...
```

公开头文件建议集中在 `engine/**/include` 或统一 `include/engine/`（实现阶段选定一种并保持稳定）。

教学文档与 ADR 位于 `render_engine/docs/learn/`，不进入 `engine/` 编译图。

## 4. 核心子系统

### 4.1 Application / Module

> **帧相位权威：** 宿主/脚本插入点以 [HOSTING.md](HOSTING.md) §4 为准。下文与之对齐。

```text
Application::Run()
  OnInit()
  while running:
    Platform.PumpEvents()
    Input.PollAndDispatch()      # 外设：热插拔 + Action
    Net.Pump()                   # 网络完成回调
    Asset.PumpAsync()            # 异步加载完成/失败仅于此后可见（先于玩法 Module）
    Module.OnUpdate(dt)          # 玩法 / game_kit / 脚本 Tick 默认插这里
    # —— 以下为引擎 World 相位（可再拆 Module，但顺序固定）——
    VideoTexture.Tick()          # 若启用
    AudioSystem.Tick()           # 若启用
    Physics.Step(dt)             # → 同步 Scene Transform（权威侧约定）
    World.Update(dt)             # 动画、Transform（含 Morph）
    RenderScene.Extract()        # SoA 快照；渲染不写权威树
    RenderSystem.Frame()         # 见 §5 Pass 列表
  OnShutdown()
```

- `IModule`：可选依赖声明，按依赖序初始化/更新/销毁。  
- 产品入口：`ENGINE_MAIN(MyApp)` 或显式 `Application` 子类。  
- **编辑器循环**为变体（先 EditorUI / 编辑相机；Play 时再挂 game_kit）。循环细节**不在本文**；见工作区独立 `editor/` 工程。  

### 4.2 外设接入层（Input）

与 `platform`（窗口/消息）分离：窗口负责消息泵与焦点；**Input 负责外设抽象与动作语义**。

```text
InputSystem
  ├─ DeviceHub          # 枚举、热插拔事件、设备句柄
  ├─ Adapters           # Win32 键鼠 / XInput·GameInput 手柄 / …
  ├─ StateStore         # 本帧按下、轴、指针、连接状态
  └─ ActionMap          # Action ↔ 绑定（键/按钮/轴），可重绑、可存盘
```

| 能力 | 一期 | 说明 |
|---|---|---|
| 键盘 / 鼠标 | 实装 | 按键、修饰键、鼠标按钮、相对/绝对移动、滚轮；与窗口焦点联动 |
| 手柄 | 实装 | 至少 XInput 或 GameInput 一路；多手柄索引、震动接口预留 |
| 动作映射 | 实装 | `Move/Look/Boost/...` 逻辑动作；配置文件加载；Sandbox/相机控制器使用 |
| 热插拔 | 实装 | 连接/断开事件；设备列表可查询 |
| 触控 / 数位板 | 接口 | `IPointerDevice` 扩展点 |
| MIDI / 自定义 HID | 接口 | `IInputAdapter` 插件注册 |
| XR 控制器 | 接口 | 位姿+按钮语义预留，实装后置 |
| 原始 HID 报表 | 后置 | 需要时经适配器插件接入，不污染核心 |

业务侧推荐只读 **Action**（或高层 `GetAxis("LookX")`），避免散落 `VK_*` / `XINPUT_*`。

### 4.3 RHI

抽象对象（名称以实现为准）：

| 对象 | 说明 |
|---|---|
| `IDevice` | 设备、能力查询、资源创建 |
| `IQueue` | Graphics / Compute / Copy |
| `ICommandList` | 录制绘制、调度、屏障、拷贝 |
| `ISwapchain` | 呈现、Resize、缓冲个数 |
| `IBuffer` / `ITexture` / `ISampler` | 资源 |
| `IShader` / `IPipeline` | 图形与计算管线 |
| `IDescriptorSet` / Layout | 绑定布局（对齐 DX12/VK 心智） |
| `IFence` 等 | CPU/GPU 同步；多帧 in-flight |

能力查询示例：`Feature::RayTracing`、`Feature::DLSS`、`Feature::MeshShader`（后置位）。

### 4.4 FrameGraph

- Pass 声明读写资源与用法（RT、DS、SRV、UAV、Present）。
- Compile：推导依赖；插入屏障点（**先 D3D12 实装，再 Vulkan 对齐**；经 RHI，业务不直调后端）。
- Execute：按序提交。
- 质量档可裁剪 Pass（关 Bloom、关 RT、降级阴影等）。
- **Lit 路径（M26 / C01）**：产品不透明照明为 **Forward+**（无 deferred G-buffer）。Pass 名冻结表见 [FORWARD_PLUS.md](FORWARD_PLUS.md)（`ShadowCSM` → `OpaqueLit` → `PostSSAO_TAA` 等）。

### 4.5 场景 → RenderScene

```text
World
  └─ Node (TRS)
       ├─ MeshRenderer
       ├─ Camera
       ├─ Light
       ├─ ReflectionProbe
       ├─ SkinnedMesh / Animator（渲染向）
       └─ Effect*

Visibility.Cull(camera) → RenderScene
  Lights, Probes, OpaqueDraws, AlphaDraws, ShadowCasters,
  Particles, Decals, Environment*, PostStack*
```

业务可跳过高层，直接向 FrameGraph/RHI 提交（高级扩展路径）。

**数据布局（ADR 0024 / STANDARDS §15）：**  
- `World/Node` 为层级与序列化权威（**非全量 ECS**）。  
- 每帧剔除后写入紧凑 `RenderScene` / 实例与可见列表（**SoA 或连续数组**），禁止深树随机访问作为提交热路径。  
- 规模优先：实例化、间接绘制、GPU Cull；局部 ECS 仅在有测量证据时另立 ADR。

### 4.6 Environment / Camera / Material

- **Environment**：Skybox（独立 cubemap SRV + `enable_skybox`）、IBL pack、雾、清除色、曝光与后期默认、环境光强度；Sandbox 启动可 `ApplyEnvironmentDefaults` 灌入 EffectTuning。
- **Camera**：透视/正交、Jitter、层掩码、输出目标、控制器（Orbit/Fly）。
- **Material / Instance**：参数与纹理覆写；Keyword 变体 → PSO 缓存。
- **队列**：Opaque / Masked / Transparent / Additive（排序规则固定，见实现文档）。

### 4.7 阴影 / GI / 探针

- 方向光 **CSM** 为光栅阴影主路径。
- **反射探针**（盒/球）提供局部反射。
- **ProbeVolume（W-gi-deepen / M22）**：CPU 密网格 irradiance；帧预算增量更新 + 三线性采样；Sandbox F1「Probe GI」叠加 ambient（**不替代** IBL / Lightmap / Sky；**不宣称 DDGI**）。见 [docs/gi/README.md](gi/README.md)。
- **Lightmap / 简化烘焙 GI**：tools 烘焙 + 运行时采样；Sandbox F1「Lightmap」可与 Probe GI 并存（albedo 乘算开关）。
- **2D 深度（M21）**：`TilemapStreamer::ExpandResidentToSprites`；`SkeletonClip2D`；雾 tint / BMFont JSON stub / CameraShake2D。
- 光追开启时可用 RT 阴影/反射等示范路径，失败则降级。

### 4.8 VFX / Post / Upscale / RT

| 模块 | 内容 |
|---|---|
| VFX | CPU 粒子、GPU 粒子、Trail、Decal |
| Post | Tonemap、ColorGrading、Bloom、FXAA、Vignette；可插拔 Effect |
| Upscaler | `IUpscaler`：DLSS→FSR2→`builtin_bilinear`（ADR 0044；无 SDK 诚实） |
| RayTracing | BLAS/TLAS、RT Pipeline；一期示范级；可关 |
| Frame Generation | **仅接口预留，一期不实装** |

### 4.9 Debug

- DebugDraw、Debug 视图模式（线框/Albedo/Normal/Roughness/Cascade/RT…）
- 控制台命令（如 `r.bloom`、质量档、视图模式）；面板优先挂 **ImmediateUi（ImGui）**
- Validation、PIX 事件命名、Profiler、截图 Readback

### 4.10 视频纹理（随后端硬解，无跨 API/软解降级）

```text
VideoTexture / VideoPlayer
  ├─ Demux（容器 → 编码码流，如 MP4 H.264/H.265）
  ├─ IVideoDecoder（按当前 RHI 后端选择实现）
  │     ├─ D3D12  → D3D12VA（共享 ID3D12Device）
  │     └─ Vulkan → Vulkan Video（共享 VkDevice / decode queue）
  ├─ 输出帧（如 NV12）→ 转换/采样为引擎 ITexture（或 SRV/采样视图）
  └─ 材质槽绑定 + Play/Pause/Loop/Seek
```

| 约束 | 说明 |
|---|---|
| 解码路径 | **跟随渲染后端**：D3D12↔D3D12VA；Vulkan↔Vulkan Video |
| 降级 | **无**软解；**禁止**跨图形 API 混用 VA；不得静默换渲染后端 |
| 失败行为 | 能力探测失败或创建失败 → 明确错误码/日志；业务可占位，引擎不改解码栈 |
| 设备共享 | 与当前渲染 Device 同设备、约定队列/同步；避免跨设备拷贝 |
| 色彩 | YUV→RGB / 色域约定写入材质与文档；HDR 视频可二期 |
| 音频 | **M7 起**解码+设备输出可验收（不做特效，ADR 0013）；产品级 A/V sync 可后置 |
| 落地节奏 | M7：D3D12VA；M17/M18：Vulkan Video 对齐 |

能力查询：`Feature::VideoTexture`（可细分 `VideoTextureD3D12VA` / `VideoTextureVulkan`）。无硬件/扩展支持时应 **失败可诊断**，而非黑屏假成功。

### 4.11 音频层（解码 + 输出渲染，无特效）

```text
AudioSystem
  ├─ AudioDecoder     # 文件/容器音轨 → PCM（或解码器输出缓冲）
  ├─ AudioClip        # 解码后的可播放资源（可流式或整段）
  ├─ AudioSource      # 播控：Play/Pause/Stop/Loop/Seek、增益
  └─ AudioOutput      # 设备输出渲染（如 WASAPI）；多源简单增益混合 → 主音量
```

| 做 | **明确不做**（音频特效，非延期） |
|---|---|
| 资源解码、缓冲、播控 | EQ、压缩、混响、合唱等 DSP 效果器 |
| 音量 / 静音 / 循环；多源简单增益相加 | 效果器总线、侧链、调音台产品化 |
| 可选与视频基础时钟对齐 | 完整 3D 空间音频、HRTF、多普勒 |

与 `VideoTexture` 同属 `media/`：视频管画面，音频管声音；二者可共享 Demux/时钟，但音频输出不经过图形 RHI。

### 4.12 网络层（传输）

```text
NetSystem
  ├─ IHttpClient       # HTTPS GET/POST、头、超时、错误码
  ├─ IWebSocket        # 连接 / 收发文本·二进制 / 关闭
  ├─ IQuicEndpoint     # QUIC 连接 + 可靠流（stream）收发
  └─ Pump()            # 主线程派发完成/消息回调
       └─ 三方：cpp-httplib · IXWebSocket · MsQuic（可替换，见 THIRD_PARTY / ADR 0021）
```

| 约束 | 说明 |
|---|---|
| 范围 | 传输与连接生命周期；**非** 帧同步 / 实体复制 / 匹配 / 反作弊 |
| 平台 | Windows + Linux；与 D3D12/Vulkan 无关 |
| TLS | HTTPS / WSS / QUIC 均须可开 TLS；证书与信任策略可配置 |
| 线程 | IO 不出主渲染线程；回调经 `Pump` 回主循环（或文档约定的工作线程） |
| 失败 | 超时、握手失败、对端关闭 → 明确错误；禁止静默丢事件 |

### 4.13 物理层

```text
IPhysicsWorld
  ├─ RigidBody / Collider / Trigger
  ├─ CharacterController
  ├─ Raycast / ShapeCast / Overlap
  └─ DebugDraw hooks
       └─ Backend: Jolt（推荐）或 PhysX
```

- 每帧：`Physics.Step(dt)` → 写回（或读）Scene Node Transform（约定权威侧）。  
- 与渲染解耦：物理不依赖 D3D12；调试形状走 DebugDraw。  
- 详见 ADR：第三方物理、禁止自研求解器。

### 4.14 通用渲染补强（P0 / P1）要点

| 级别 | 能力落点 |
|---|---|
| P0 | 点/聚光 Shadow Atlas；TAA；SSAO/GTAO；透明策略；LOD；实例化；流式+预算；遮挡剔除 |
| P1 | SSR；DoF；运动模糊；自动曝光；体积雾；动态 Cubemap/Planar；Morph；扩展着色；间接绘制/GPU Cull；Bindless；多线程录制；HDR 输出；色彩管理 |
| **P2（缺口补齐）** | 混合阴影/MV、分层后处理、统一拣选；2D 骨骼/流式 Tilemap/光雾等；**动态 GI**；地形/水体/植被基础；Mesh Shader/增强 GPU Driven；VK RT 对齐 |

质量档必须能开关上述重特性。P0/P1 见 M10–M14；P2 见 [PLAN.md](PLAN.md) M20–M25。

### 4.15 UI 层

```text
UiSystem
  ├─ ImmediateUi     # Dear ImGui → 调试/工具/控制台面板
  ├─ RetainedUi      # RmlUi（推荐）或薄自研：HUD/菜单/文档流布局
  ├─ FontService     # 字体加载、图集、DPI
  └─ UiPass          # FrameGraph：3D/后处理之后绘制 UI
```

| 约定 | 说明 |
|---|---|
| 输入 | `Ui.WantCaptureMouse/Keyboard` 为真时，不把对应输入送给相机/玩法 Action |
| 分辨率 | 逻辑 UI 尺寸与 framebuffer/DPI 缩放分离 |
| 后端 | UI 网格/纹理提交走引擎 **RHI**（先 D3D12，再 Vulkan）；不另开第二套 Present |
| 不做 | 独立可视化 UI 编辑器 |

### 4.16 2D / 像素 / 混合渲染

```text
Render2D
  ├─ Sprite / Billboard
  ├─ SpriteAtlas + FrameAnimator（渲染事件钩子可选）
  ├─ PixelSettings（Nearest、整数缩放、网格对齐）
  ├─ SortLayer / YSort（与透明队列协同）
  └─ TilemapRenderer（多图层；碰撞层数据可给 Physics，无玩法逻辑）
Camera
  └─ Orthographic / Axonometric helpers
Post（可选）
  └─ Palette / LUT、简易 2D 法线光
```

- 与 3D Opaque/Alpha Pass 的插入点在 FrameGraph 中文档化（如 AfterOpaque Actors、BeforeUI）。  
- **不包含** 对话/背包/任务/战斗等玩法模块。

## 5. 标准帧管线（RenderSystem.Frame 内）

> 对应 §4.1 中 `RenderSystem.Frame()`。**逻辑相位**（Pump / Module / Physics / Extract）见 §4.1 与 [HOSTING.md](HOSTING.md)，不在此重复打乱。

1. （Extract 已在 Frame 前完成）可见性剔除（视锥 + 遮挡）→ 使用本帧 `RenderScene`  
2. Shadow（CSM + 局部光 Atlas；或 RT 阴影若开启）  
3. Opaque（PBR + IBL + 探针 + Lightmap + SSR 等；可含视频材质）  
4. **2D/Sprite/Tilemap 层**（按 SortLayer / Y-sort 与深度策略）  
5. Sky / Environment（含体积雾）  
6. Alpha / Additive VFX（按透明策略）  
7. Motion Vectors（TAA/超分需要时）  
8. PostProcess（含可选像素 LUT）+ Upscale  
9. Debug 视图 / DebugDraw（含碰撞体、Tile/像素网格）  
10. **UI Pass**（Retained HUD/菜单 + Immediate 调试叠加）  
11. Present（含 HDR 输出路径若启用）  

GPU 上传中与异步收割重叠的部分：在 `Asset.PumpAsync` 已登记的上传，于本帧或 Fence 安全点提交（RUNTIME_FOUNDATIONS）。  

## 6. 后端策略

| 后端 | 平台 | 状态 |
|---|---|---|
| **D3D12** | Windows | 完整实装（功能基准之一） |
| **Vulkan** | Windows、Linux | 完整实装（与 D3D12 主路径对齐；见能力差） |
| D3D11 / OpenGL / GLES | — | stub / NotImplemented |
| Metal / 移动 API | — | **明确不做** |

**能力差（须文档化并 QueryFeature）：**

| 特性 | D3D12 | Vulkan |
|---|---|---|
| 视频纹理 | D3D12VA stub 可诊断（真解另波） | Vulkan Video（产品路径外置） |
| 光追示范 | DXR 真 AS/DispatchRays（可关） | 有 `rayTracingPipeline` 则示范否则 SKIP |
| 超分 | 可选 DLSS/FSR2；无 SDK → `builtin_bilinear` | 同左 |
| Mesh Shader | Tier→热路径 L1；无 → SKIP | EXT→热路径 L1；无 → SKIP |

D3D12：Fence、屏障、描述符、Root Sig/PSO、Copy、DXR、NGX 等封装在后端内。  
Vulkan：Swapchain、RenderPass/DynamicRendering、描述符、SPIR-V、同步封装在后端内。

**降维护约定（ADR 0024）：** 特性分 **L0（双端必齐）/ L1（可暂单端）/ L2（可永久差）**；落地顺序 **先 D3D12，再 Vulkan**；业务只查 Feature，不写双套玩法。细则见 [STANDARDS.md §15](STANDARDS.md)。

## 7. 线程与生命周期

> 契约全文：[RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md)（含 **§6 数据依赖**、**§7 生命周期**）、ADR 0026。

| 约定 | 说明 |
|---|---|
| 逻辑 / 渲染分离 | 权威 Scene 树仅逻辑写；渲染读 `RenderScene` SoA 快照（ADR 0024） |
| 渲染提交 | 默认主线程编排；**M14** 起多线程命令录制；独立 Render Thread 可选加深 |
| 异步加载 | IO/解码在工作线程；**完成/失败回调仅 `Asset.PumpAsync` 后于主线程** |
| CPU 资产寿命 | `AssetHandle` **引用计数**；流式淘汰不踢仍有引用的对象 |
| GPU 资源销毁 | **Fence 世代**退役队列，避免 in-flight 释放（ADR 0006） |
| Resize | 重建 Swapchain/依赖尺寸的 RT；业务通过事件感知 |
| Device Removed | 记录错误并走可控失败路径（文档化） |
| Profiling | CPU 区间 + **GPU Pass 时间戳**（M8–M9）；PIX/RenderDoc 事件名 |

## 8. 质量档（逻辑）

| 档位 | 典型裁剪（示例） |
|---|---|
| Low | 关 RT、关/降 Bloom、单级或无 CSM、关超分或最低档、降 IBL 级别 |
| Medium | CSM 中等、光栅阴影、可选超分、标准 IBL |
| High | 全后处理、完整 IBL、CSM 高质量；RT/软影 **opt-in**；**不**默认开 DLSS（已冻结） |

具体映射在配置与 `RenderSystem` 中实现，Sandbox 可运行时切换。

## 9. 扩展点

1. 注册自定义 `IModule`  
2. 注册 FrameGraph Pass（插入标准管线槽位：Shadow/Opaque/AfterOpaque/Post…）  
3. 扩展 `IPostProcessEffect` / `IUpscaler`  
4. 新增 Material Keyword 与 Shader 变体  
5. 后续：注册新 `IBackend` 实装  
6. 注册 `IInputAdapter`（自定义外设）  
7. `VideoTexture` 播控扩展（仍必须走当前后端硬解）  

## 10. 相关文档

- [../README.md](../README.md) — `render_engine` 说明  
- [../../README.md](../../README.md) — 工作区根  
- [../../docs/LAYERS.md](../../docs/LAYERS.md) — **工作区分层权威**  
- [HOSTING.md](HOSTING.md) — 玩法层 / 脚本 / 编辑器外挂  
- [HOST_API.md](HOST_API.md) — Host API v0  
- [PREFAB_SCHEMA.md](PREFAB_SCHEMA.md) — Prefab/场景 schema  
- [GETTING_STARTED_M1.md](GETTING_STARTED_M1.md) — M1 可执行清单  
- [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md) — Cook/异步/线程/寿命/GPU Profiling  
- [TOOLING.md](TOOLING.md) — 工具链（必要 / 可后置 / 不做）  
- [PLAN.md](PLAN.md) — 范围、里程碑、验收  
- [STANDARDS.md](STANDARDS.md) — **编码 / 架构 / 模块通讯等规范**  
- [POSITIONING.md](POSITIONING.md) — 定位、缺陷与风险  
- [KNOWN_GAPS.md](KNOWN_GAPS.md) — 已知缺口与后续候选  
- [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md) — 调试 / 调优 / 排错  
- [THIRD_PARTY.md](THIRD_PARTY.md) — 第三方库引入清单  
- [TESTING.md](TESTING.md) — 单测 / 集成 / 自动化  
- [learn/README.md](learn/README.md) — 教学封装层  
