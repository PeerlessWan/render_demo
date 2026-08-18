# 实施计划

> 依据 [ARCHITECTURE.md](ARCHITECTURE.md) 与 [POSITIONING.md](POSITIONING.md)  
> 教学封装：[learn/README.md](learn/README.md) · 路径：[learn/PATH.md](learn/PATH.md)  
> 定位：**Windows（D3D12 + Vulkan）/ Linux（Vulkan）通用 2D·3D 渲染引擎**  
> 范围分段：M1–M16（既有能力）+ M17–M19（后端/Linux/网络）+ **M20–M25 引擎缺口补齐（P2）**。  
> **当前迭代看板**（Doing / Undo / Todo）：[DOING_UNDO_TODO.md](DOING_UNDO_TODO.md)  
> **引擎状态：Mega-W11 已收口**（拉齐各端 / 引擎 only；[ADR 0038](learn/adr/0038-mega-w11-parity.md)；**未改** game_kit/editor）。Mega-W10 见 [ADR 0037](learn/adr/0037-mega-w10-deepen.md)。  
> **游戏可用 ≠ 渲染可用**：工作区可玩产品水位见 **§1.9**（主缺口 `game_kit` GK0–GK3）。

## 1. 总验收目标

交付可运行的 **通用 2D·3D 渲染引擎**（Windows：D3D12+Vulkan；Linux：Vulkan），含网络传输与 **P2 缺口补齐**，含：

### 1.1 基础段（原 M1–M9）

- 引擎主循环 + Module；外设接入（键鼠/手柄/ActionMap）  
- 场景 / 相机 / Environment / PBR+完整 IBL / CSM / 探针 / 简化 Lightmap GI  
- 蒙皮、粒子/Trail/Decal、后处理基础集、质量档、调试与序列化  
- DLSS+FSR fallback；DXR 示范+可关降级  
- 视频纹理 **跟随渲染后端硬解**（D3D12→D3D12VA；Vulkan→Vulkan Video；无软解/跨 API 降级）；音频 **仅解码+输出（明确不做特效）**  
- 教学封装双轨  
- **UI（起步）**：控制台/调试面板可依赖即时模式 UI 雏形（与 M8 控制台协同）



### 1.2 通用渲染补强（P0 + P1）与物理

**P0（通用渲染刚需）**


| 项           | 说明                             |
| ----------- | ------------------------------ |
| 点光/聚光阴影     | Shadow Atlas（或等价）；与方向光 CSM 并存  |
| TAA         | 生产级抗锯齿主路径（可与超分/Jitter 协同）      |
| SSAO 或 GTAO | 至少一种屏幕空间 AO                    |
| 透明策略        | 文档+实现：排序规则；复杂交叉的近似或 OIT/加权方案择一 |
| 网格 LOD      | LOD 组与距离/屏幕占比切换                |
| 实例化 / 合批    | 同材质实例绘制为正式提交路径                 |
| 资源流式 + 内存预算 | 纹理/网格流式加载、预算与淘汰                |
| 遮挡剔除        | 视锥之外至少一种（Hi-Z 或软件遮挡）；接口可扩展     |


**P1（通用标配）**


| 项                  | 说明                                    |
| ------------------ | ------------------------------------- |
| SSR                | 屏幕空间反射                                |
| DoF + 运动模糊         | 后处理扩展                                 |
| 自动曝光 / 眼适应         | HDR 管线闭环                              |
| 体积雾                | Environment 升级（可含简易体积光接口）             |
| 动态反射               | 实时 Cubemap 与/或 Planar Reflection 至少一种 |
| Morph / BlendShape | 与蒙皮并存                                 |
| 扩展着色槽              | Clearcoat、薄透射等可插拔模型（按优先级落地 1–2 个）     |
| 间接绘制 / GPU Cull    | 大规模提交扩展点落地                            |
| Bindless 或大描述符模型   | 材质绑定扩展                                |
| 多线程命令录制            | 在 RHI/后端落地（非仅接口）                      |
| HDR 显示输出           | 与内部 HDR 区分的显示器 HDR 路径                 |
| 色彩管理               | 工作色域与输出变换约定并实现                        |


**物理引擎**


| 项    | 说明                                                       |
| ---- | -------------------------------------------------------- |
| 集成方案 | **第三方实现** + `IPhysicsWorld` **抽象**（推荐 Jolt；PhysX 可替换适配层） |
| 能力   | 刚体、碰撞形状、触发器、Raycast/ShapeCast、场景 Transform 同步、碰撞调试绘制     |
| 角色   | Character Controller（胶囊体）纳入物理层，供通用交互                     |
| 不做   | 完整载具轮胎模型、破坏专用求解器产品化；**服装级**布料管线（薄 SoftBody/Cloth 见看板 **W-phys-soft** / KNOWN_GAPS **C22**） |




### 1.3 UI 层


| 项            | 说明                                                                                |
| ------------ | --------------------------------------------------------------------------------- |
| 调试 / 工具 UI   | **Dear ImGui**（或等价即时模式）经 **RHI** 接入（先 D3D12，M17+ Vulkan）：控制台、Profiler 面板、属性/质量档开关 |
| 运行时 / HUD UI | **保留模式**一层：推荐 **RmlUi**（或自研薄保留模式）；菜单、HUD、文本、图片、基础控件                               |
| 合成           | FrameGraph **UI Pass**：在 3D/后处理之后、Present 之前叠加；支持 DPI 缩放                          |
| 输入           | 与外设接入层协作：`WantCapture` 时阻断相机/游戏 Action；手柄可导航 UI（基础）                               |
| 字体           | 字体加载与图集；中英文基础字形                                                                   |
| 不做           | UMG 级可视化 UI 编辑器、完整 CSS/浏览器引擎、复杂动画时间轴产品化                                           |




### 1.4 2D / 像素 / 2D·3D 混合渲染（仅引擎能力，不含玩法）

面向「像素风 + 2D/3D 混合」等内容形态，补齐**渲染与场景侧**能力。  
**明确不包含**：对话/背包/任务/战斗/数值 RPG 系统、寻路 AI、玩法存档等（→ `game_kit` / `genre_kits` / `games`，见 [LAYERS](../../docs/LAYERS.md)、ADR 0028）。


| 项          | 说明                                                      |
| ---------- | ------------------------------------------------------- |
| 精灵 / 公告板   | 世界空间 Sprite、Camera-facing Billboard；合批提交                |
| 像素管线       | **Nearest** 采样默认；**整数倍缩放**；可选 **像素网格对齐**（减亚像素抖动）        |
| 图集与 2D 动画  | Atlas / SpriteSheet；帧动画（时间轴、循环、事件可选钩子仅渲染层）              |
| 混合排序       | 与 3D 透明队列协同：Y-sort、分层（如 Ground/Actor/Overlay）、深度测/写策略可配 |
| 相机模式       | 正交；固定轴测/斜视辅助（与透视相机可切换）                                  |
| Tilemap 渲染 | 多图层瓦片绘制；Tiled JSON（或等价）**渲染导入**；碰撞层数据可导出给物理（不做 RPG 逻辑）  |
| 像素美学可选     | 限色/调色板或 LUT 后处理开关；可选法线精灵的简化 2D 光照（非完整 PBR）              |
| 遮挡表现       | 屋顶/前景层淡出或裁剪（渲染层，非任务脚本）                                  |
| Debug      | 像素网格、图集 UV、Tile 边界、排序层可视化                               |




### 1.5 网络层（传输，非玩法同步）

面向资源拉取、遥测、轻量 RPC/信令等；**不含** 状态同步、匹配服、反作弊。


| 项         | 说明                                                                                                                                                                                                              |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| HTTP(S)   | 客户端为主（GET/POST、头、超时、TLS）；可选极简嵌入式服务端（调试/工具）                                                                                                                                                                      |
| WebSocket | 双向消息（文本/二进制）；TLS（`wss`）；连接/断开/错误可诊断                                                                                                                                                                             |
| QUIC      | **可靠传输**（QUIC 可靠流）；用于需可靠、可多路复用的应用数据；TLS 1.3 内置于 QUIC                                                                                                                                                            |
| 封装        | 引擎 `net/` **抽象 API**（`IHttpClient` / `IWebSocket` / `IQuicEndpoint`）；业务不直链三方头；实现可替换                                                                                                                             |
| 三方实现      | **轻量跨平台**默认：HTTP → [cpp-httplib](https://github.com/yhirose/cpp-httplib)；WS → [IXWebSocket](https://github.com/machinezone/IXWebSocket)；QUIC → [MsQuic](https://github.com/microsoft/msquic)（可换 ngtcp2 等，须 ADR） |
| 线程模型      | IO 在后台线程或库线程；主循环 `Net.Pump()` 派发完成回调（与 Application 协同）                                                                                                                                                          |
| 平台        | Windows + Linux；与图形后端无关                                                                                                                                                                                         |
| 不做        | 帧同步/实体复制、大厅匹配、反作弊、完整 gRPC/HTTP3 浏览器栈、自研协议密码学                                                                                                                                                                    |




### 1.6 图形后端与平台


| 平台          | 支持后端                                          |
| ----------- | --------------------------------------------- |
| **Windows** | **D3D12**、**Vulkan**（运行时可选）                   |
| **Linux**   | **Vulkan** only                               |
| 明确不做        | **macOS / iOS / Android** 及任何移动端；**Metal** 不做 |
| 其它          | D3D11/GL/GLES：**仅 stub**（不实装）                 |



| 约定    | 说明                                                                             |
| ----- | ------------------------------------------------------------------------------ |
| 默认    | Windows 默认 D3D12；可用配置/命令行切 Vulkan                                              |
| Linux | 窗口：X11 必做，Wayland 推荐同里程碑或紧随；仅 Vulkan 创建设备                                      |
| 能力差   | **DXR** 主路径可仅在 D3D12（Vulkan RT 在缺口补齐段对齐）；**视频**跟随渲染后端，驱动不足时 Feature=false，不做软解 |
| 视频硬解  | **D3D12→D3D12VA**；**Vulkan→Vulkan Video**；与 Device 绑定                          |
| 超分    | DLSS：按厂商支持的 API（常见 D3D12/VK）；FSR/内置作 fallback，两后端都要能关超分跑通                      |
| 着色器   | HLSL→DXIL（D3D12）；DXC→SPIR-V（Vulkan）；业务仍走引擎 Shader 模块                           |




### 1.7 引擎缺口补齐（P2，对标主流通用渲染短板）

在 M1–M19 之上补齐 [KNOWN_GAPS](KNOWN_GAPS.md) 中的 **引擎向**缺口。仍不做玩法、脚本 VM、完整可视化编辑器、状态同步。


| 簇         | 项                | 说明                                                                  |
| --------- | ---------------- | ------------------------------------------------------------------- |
| **混合打磨**  | 混合阴影 / 运动向量      | 精灵可接收 3D 阴影；2D 层可写 MV 供 TAA/超分                                      |
|           | 分层后处理            | 3D 与像素层可分开（如 2D 不做 AO）                                              |
|           | 统一拣选             | 2D+3D 拾取、高亮、调试一致                                                    |
|           | 像素多分辨率           | 非整数窗口、多 DPI 规则与实现                                                   |
| **2D 深度** | Tilemap chunk 流式 | 大地图分块加载与渲染                                                          |
|           | 2D 骨骼动画          | Spine/DragonBones 类渲染向导入与播放（抽象层）                                    |
|           | 动画瓦片 / 自动地形表现    | Tilemap 表现力                                                         |
|           | 2D 光与迷雾          | 多光、黑暗遮罩等                                                            |
|           | 世界空间文字           | BMFont/等价                                                           |
|           | 2D 相机特效          | 震屏、Letterbox、分屏基础                                                   |
|           | 2D 粒子排序/合批       | 与 SortLayer 集成                                                      |
| **画质/规模** | 动态 GI            | DDGI 或等价动态探针体积（可关）                                                  |
|           | 地形 / 水体 / 植被     | 高度图地形+LOD；基础水面；实例化草/树基础                                             |
|           | GPU Driven 增强    | Mesh Shader 或更强 GPU Cull（按 Feature）                                 |
|           | 光追对齐             | Vulkan RT 对齐 D3D12 示范级                                              |
| **可选后置**  | 矢量/路径、宿主加深等      | **不阻塞** M1–M25；见 [KNOWN_GAPS.md](KNOWN_GAPS.md) **§4（C01–C21、G13）** |




### 1.8 工具链（离线 / 构建期，最小可行）

**引擎内**不做完整可视化内容编辑器；以 **外部 DCC +** `tools/` **CLI + 运行时导入** 为默认闭环；独立 `editor/` 见 HOSTING。细则：[TOOLING.md](TOOLING.md)、ADR 0025。


| 紧迫           | 工具 / 约定                                                         | 对齐                                       |
| ------------ | --------------------------------------------------------------- | ---------------------------------------- |
| **必要**       | `shader_compile`（DXC→DXIL/SPIR-V）                               | **M2**（M1 清屏可不依赖）                        |
| **必要**       | AssetId / VFS 路径约定                                              | M2–M3                                    |
| **必要**       | `ibl_baker`（IBL 三件套）                                            | M5                                       |
| **必要**       | 纹理压缩路径（BC/DDS 或 KTX；可封装 DirectXTex）                             | M5–M10                                   |
| **必要**       | 最小 asset cook / 资产清单 + **依赖图** + 可选打包                           | M3 定约定，**M9 前落地**（见 RUNTIME_FOUNDATIONS） |
| **较必要**      | `lightmap_baker`（可简陋）                                           | M6–M8                                    |
| **必要**       | 黄金图跑测脚本                                                         | M9                                       |
| **保留（冻结）**   | SandboxHarness JSON 控制面；`run_matrix_smoke.py` 抽样                | 已落地；**保留、不再加命令**（见 §3.21）                |
| **冻结（可选）**   | `sandbox_mcp` Cursor 适配器                                        | 已落地；**不扩功能**；CI 不依赖；本机不用可删               |
| **必要（2D）**   | 图集格式约定（外部打包）；Tiled JSON 导入                                      | M16 前 / M16                              |
| **可后置**      | meshoptimizer 离线 LOD；Feature 导出小工具；地形数据校验                       | M10+ / M23                               |
| **可后置（C20）** | Manifest/AssetId 浏览器、场景 JSON 校验、依赖图检查 CLI                       | 不挡 M1–M9；可先于视口 `editor/`（C21）            |
| **不做（引擎内）**  | 关卡/材质节点/UI 可视化编辑器进 `engine/`；FBX/USD 一站式；NavMesh/音频中间件工具；自研帧调试器 | 独立 `editor/` 见 HOSTING / ADR 0027        |




### 1.9 2D/3D 游戏可用水位（工作区）

> 对标 **Godot / Unity 中小 PC 项目**，不是 UE5 开放世界。  
> **渲染内核 ≠ 可玩游戏。** Sandbox 能演示画面；可玩产品 = `game_kit` + `render_engine`（± `editor` / `genre_kits` / `games`）。分层：[LAYERS](../../docs/LAYERS.md)、[HOSTING.md](HOSTING.md)。

**现状：** D3D12 主路径（场景/PBR/阴影/后处理/天空盒/2D Sprite·Tilemap/物理刚体/输入/HUD）已达「桌面渲染内核」可用加深。`game_kit` / `editor` / `genre_kits` **仅文档、无代码**。

#### 口径

| 口径 | 含义 | 不宣称 |
|---|---|---|
| **渲染可用** | Win D3D12 能出 2D/3D 游戏画面 | 双后端发版、手机、开放世界 |
| **游戏可用** | 一条 2D **或** 3D 小关能走完：切关、暂停、存档槽、脚本不毁 Device | Godot/Unity 开箱模板 |
| **对标主流** | 编辑器摆关、Prefab、动画树、寻路、空间音频、热重载 | Lumen/Nanite/复制同步（明确不做或外挂） |

#### 缺口（按痛，非渲染再堆 Pass）

| 优先级 | 缺什么 | 层 | 里程碑 |
|---|---|---|---|
| **P0** | 关卡流 / Entity / 事件 / 暂停 / 存档槽 | `game_kit` | **GK0–GK1**（无代码） |
| **P0** | 脚本白名单绑定；异常不毁 GPU | `game_kit` | **GK2** |
| **P0** | 垂直切片：移动、触发、HUD | `game_kit` | **GK3** |
| **P1** | Prefab + Manifest 同一套 | `game_kit` + 引擎 schema | **GK4** |
| **P1** | 视口摆关（独立工程） | 工作区 `editor/` | **文档不在本目录** |
| **P1** | 动画混合 / 简单状态机 | 引擎候选 **C10** 或上层自建 | 不进 M1–M25 除非另开 |
| **P1** | 真 RmlUi / 字体 DPI | 引擎 M15 加深 | 阻塞：vendor |
| **P1** | Win Vulkan 观感对齐 | 引擎 | **W-vk-parity**（看板） |
| **P2** | 寻路 | **中间件**，不进 `engine/` | — |
| **P2** | 2D 骨骼产品化 | 引擎 M21 加深 + 许可 | — |
| **P2** | 3D 衰减/混音 | **不做引擎 DSP**；上层或中间件 | ADR 0013 |
| **P2** | 资源/着色器热重载 | **C16** | M25 后候选 |

**明确不追齐（对标主流会一直弱）：** mac/移动/Metal；引擎内脚本/完整编辑器；VT/Nanite；Lumen 级 GI；复制/匹配；材质节点图；FBX/USD 一站式；Linux 外置至 M18。

#### 落地顺序（工作区）

```text
1. game_kit GK0–GK3     ← 游戏可用的主缺口（优先于大气/云/Bindless 全量）
2. Prefab + 场景能存     ← 手改 JSON 可先发；GK4
3. 动画状态机 C10 或上层自建
4. W-vk-parity          ← 仅当要 Win 双后端发版
5. C20 CLI（引擎 tools 候选）→ 视口编辑器见工作区 editor/（规格不在本目录）
6. 品类 → genre_kits；内容 → games/<title>
```

引擎近端债（VK 对标、GI 加深、薄 SoftBody）见看板，**不替代** GK0。`game_kit` 细则：[game_kit/docs/PLAN.md](../../game_kit/docs/PLAN.md)。编辑器排期与规格**只写在**工作区 `editor/docs`，不进 `render_engine/docs`。

## 2. 范围清单



### 2.1 必做（… + Vulkan + Linux）


| 域           | 项                                                                                                                                       |
| ----------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| 运行时         | Application、ModuleSystem、Config、Log、Version、Math、Clock、Event                                                                            |
| 平台          | **Windows**：Win32 窗口；**Linux**：X11（+Wayland 目标）；DPI、Resize、消息/事件泵                                                                       |
| 图形后端        | **D3D12（Windows）**、**Vulkan（Windows + Linux）**；工厂按平台/配置选择；D3D11/GL/GLES stub                                                            |
| 资源          | VFS、AssetId、**Manifest/依赖图/可选打包**、**异步加载+主线程回调**、**Handle 引用计数**、上传环与 Fence 寿命、PNG/JPEG/(DDS)、glTF 静态+蒙皮、**流式与内存预算**、Shader 编译、Fallback |
| 视频纹理        | **跟随渲染后端**：D3D12→D3D12VA；Vulkan→Vulkan Video；无软解、不跨 API 降级                                                                              |
| 音频          | **仅**解码+输出+简单增益混合；**明确不做音频特效**（ADR 0013）；可选与视频基础时钟对齐                                                                                    |
| 场景          | World/Node、视锥+**遮挡剔除**、**LOD**、**实例化收集**、序列化                                                                                            |
| 动画          | 骨骼蒙皮、片段、**Morph/BlendShape**                                                                                                            |
| 渲染          | RHI、FrameGraph、**RenderScene 抽取（逻辑/渲染分离）**、**多线程录制（M14）**、RenderSystem、Camera、Environment                                               |
| 材质          | Material/Instance、PBR、变体、**扩展着色槽**、**Bindless/大描述符**                                                                                    |
| 光照          | IBL、CSM、**点/聚光阴影**、探针、Lightmap、**SSR**、**动态反射**                                                                                         |
| 可见性/提交      | **间接绘制 / GPU Cull**                                                                                                                     |
| 特效          | CPU/GPU 粒子、Trail、Decal                                                                                                                  |
| 后处理         | Tonemap、ColorGrading、Bloom、FXAA、**TAA**、**SSAO/GTAO**、**DoF**、**运动模糊**、**自动曝光**、**体积雾**；可插拔                                             |
| 超分          | DLSS + FSR/内置 fallback                                                                                                                  |
| 光追          | DXR 示范 + 可关降级                                                                                                                           |
| 显示/色彩       | **HDR 输出**、**色彩管理**                                                                                                                     |
| **物理**      | Jolt（或等价）封装：刚体、形状、触发器、查询、角色控制器、DebugDraw                                                                                                |
| **UI**      | ImGui 调试/工具 UI；保留模式运行时/HUD UI；UI Pass 合成；字体；输入捕获路由                                                                                      |
| **2D / 像素** | Sprite/Billboard、像素采样与整数缩放、图集帧动画、混合排序、正交/轴测相机、Tilemap 渲染导入、可选调色板/简易 2D 光、屋顶层淡出                                                          |
| **网络**      | HTTP(S)、WebSocket、QUIC 可靠流；**抽象层 + 轻量三方实现**；主循环 Pump；无玩法同步                                                                              |
| **缺口补齐 P2** | 混合阴影/MV、分层后处理、统一拣选、像素多 DPI；Tilemap 流式、2D 骨骼、2D 光雾/文字/相机特效/粒子；动态 GI；地形/水体/植被基础；GPU Driven 增强；VK RT 对齐                                    |
| **工具链**     | shader_compile、ibl_baker、lightmap_baker、纹理压缩、**cook/清单/依赖图/打包**、黄金图脚本；图集约定 + Tiled 导入（TOOLING + RUNTIME_FOUNDATIONS）                    |
| 调试          | DebugDraw、视图模式、控制台、**ImGui 调试**、**CPU/GPU Profiler（Pass 时间戳）**、Readback、Validation；见 DEBUG + RUNTIME_FOUNDATIONS                        |
| 质量          | Low/Med/High（含 P0/P1 与像素相关开关）                                                                                                           |
| 教学          | Learn 双轨随里程碑补                                                                                                                           |
| **测试**      | 单测 + 集成 + 自动化/黄金图（TESTING §8）；Harness 冻结；MCP 不扩；**加深策略 PLAN §3.1（准优先于广）**                                                               |
| 交付          | Sandbox（渲染验收）；**可玩游戏**走 game_kit + games（见 §1.9）                                                                                                                        |




### 2.2 明确不做（仍不进引擎）

- **macOS / iOS / Android** 及任何移动端；**Metal**
- D3D11 / OpenGL / GLES **实装**（接口 stub）  
- Frame Generation、Reflex 实装  
- **完整关卡/材质节点可视化编辑器**、导航网格产品、脚本 VM  
- **游戏状态同步 / 匹配服 / 反作弊**（网络层只做传输）  
- **可视化 UI 编辑器**  
- **音频特效**（**明确不做、非延期**）：EQ/压缩/混响/合唱等 DSP、效果器总线、HRTF/完整 3D 空间音频、多普勒等  
- 视频软解；跨后端混用 VA；播放器产品化  
- **布料/软体**：薄 SoftBody/Cloth 经 `IPhysicsWorld` 可加深（看板 W-phys-soft / C22）；**不做**服装管线与完整载具轮胎等物理产品化  
- **游戏玩法系统**  
- 商业资产生态 / 商店  
- **完整可视化内容编辑器**（关卡/材质节点/UI）及 FBX/USD 一站式工业管线（主路径 glTF；见 TOOLING）  
- **MCP 扩成编辑器 / 远程运维 / 脚本宿主**（`sandbox_mcp` 仅薄适配，见 §3.21）

说明：动态 GI、地形/水体/植被、2D 骨骼等已纳入 **M20–M25**；**macOS/移动明确不做**；**最小工具链**见 §1.8，不是「无工具」。

## 3. 实施约束（风险锁死）

1. 无 DLSS → 必须 FSR 或内置超分 fallback。
2. 无 DXR 或关闭 RT → 回退光栅阴影，禁止硬崩。
3. Frame Generation 不做。
4. **后端**：**仅** Windows D3D12+Vulkan、Linux Vulkan；**不做** macOS/移动/Metal；D3D11/GL/GLES 不实装。
5. Sandbox + 文档验收；质量档可感知差异。
6. 视频解码 **跟随渲染后端**（D3D12VA / Vulkan Video），无软解、不跨 API 降级；能力不足则明确失败。
7. **音频明确不做特效**：仅解码 + 设备输出 + 简单增益混合；无 DSP/空间音频总线（ADR 0013）。
8. 物理以第三方库 + **抽象层**封装交付（可替换实现）。
9. P0 完成前不宣称 3D 通用渲染验收通过。
10. UI：即时+保留；无可视化 UI 编辑器；三方 UI 经引擎 UI 抽象。
11. 测试：PR 过 unit；发版过 GPU 冒烟与黄金图；**D3D12 与 Vulkan 至少各有冒烟**（Linux CI 可选自托管）。**不做像素/帧级全覆盖**（抽样，见 TESTING §8）。
12. 玩法系统不进引擎核心；外挂见 [HOSTING.md](HOSTING.md)、[LAYERS](../../docs/LAYERS.md)、ADR 0028。
13. 网络：HTTP / WS / QUIC 可靠流走轻量三方 + **抽象层**；玩法同步/匹配/反作弊不进引擎。
14. **所有三方库必须经抽象/适配层**；禁止业务直链三方头（ADR 0017）。
15. 编码、架构、模块通讯等遵守 [STANDARDS.md](STANDARDS.md)（ADR 0022）。
16. **M20–M25** 补齐引擎向主流短板（混合/2D 深度/动态 GI/场景专题/GPU Driven/RT 对齐）；**不做** macOS/移动；玩法与完整编辑器仍不做进 `engine/`。
17. 双后端：特性 **L0/L1/L2** 分级；**先 D3D12 再 Vulkan**；场景热路径 **SoA 提取**（非默认全量 ECS）（ADR 0024 / STANDARDS §15）。
18. 工具链：按 [TOOLING.md](TOOLING.md) 落地最小可行集（ADR 0025）；**不做**可视化内容编辑器进引擎。
19. 运行时基础：Cook/依赖、异步回调、逻辑/渲染分离、Handle+Fence、**数据依赖与生命周期**、GPU Profiling（[RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md)、ADR 0026）。
20. 脚本与编辑器：默认外挂（[HOSTING.md](HOSTING.md)、ADR 0027）；品类分层 ADR 0028；候选 C19–C21，不进 M1–M25。
21. **Harness / MCP（冻结）**：
  - **Harness 保留**：`--harness-stdio` 为矩阵抽样稳定接口（`run_matrix_smoke.py`）；**不再加命令**。门禁准确度仍靠黄金图 + `--gpu-headless`，不靠 MCP。  
    - **MCP 冻结**：`sandbox_mcp` 仅 Cursor 薄适配；**不扩**编辑器/运维/脚本语义；CI **不依赖** MCP。本机不用 Agent 控 Sandbox 时可删 `tools/sandbox_mcp`，不影响正确性。  
    - 协议与挂法：[SANDBOX_MCP.md](SANDBOX_MCP.md)。
22. **自动化测试加深**：**准优先于广**；不扩 Harness 命令、不扩 MCP、不追像素全覆盖。实施顺序与不做项见 **§3.1**。  
23. **游戏可用 ≠ 渲染可用**：2D/3D 可玩产品以 **`game_kit` GK0–GK3 为主缺口**；不把玩法做进 `engine/`；不把大气/云当「能做游戏」的前提。口径与顺序见 **§1.9**。  
24. **编辑器文档不进引擎**：视口编辑器规格/排期只在工作区 `editor/docs`；本目录不写 ED 里程碑、不收录 editor PLAN。引擎侧仅保留 **C20**（轻量 CLI 候选）与「独立 `editor/`」边界。



## 3.1 自动化测试加深策略（准 / 广）

> 现状水位与测法：[TESTING.md](TESTING.md) §8。Harness/MCP 冻结见 §3.21。  
> 目标：提高 **失败=真回归** 的概率（准），再增加 **可断言路径**（广）。不是把 RMSE 收到 0，也不是笛卡尔积。

**原则：** 准靠去掉时间/驱动噪声；广靠小场景、双后端一致性、校验层。MCP 不参与。

### 准（优先）


| 序   | 项                     | 做法                                                                     | 验收                                                    |
| --- | --------------------- | ---------------------------------------------------------------------- | ----------------------------------------------------- |
| Q1  | **确定性截帧**             | 黄金图预设：固定 `dt`、抽干异步、GPU idle；**关 TAA/Jitter、冻粒子/物理**（用现有 `toggle`，不加命令） | 同机连续两次 RMSE 接近噪声下限；flaky 明显下降                         |
| Q2  | **Vulkan 真 Readback** | 对齐 D3D12 `ReadbackTextureStub`；VK `gpu_headless` 不再 CPU stub           | `--backend=vulkan --gpu-headless` 可出 `.rgba`；无能力 SKIP |
| Q3  | **中间缓冲黄金图**           | 法线 / 线性深度（可选阴影图）与 LDR **分文件**；中间缓冲更严阈值                                 | 至少 1 条 depth 或 world-normal 基线；失败能区分「曝光漂」vs「几何漂」      |
| Q4  | **D3D12 WARP 基线（可选）** | CI 用 WARP 锁算法；真 GPU 仍冒烟 + 发版抽样                                         | 无独显 runner 可跑黄金图；文档写明 WARP ≠ 游戏 GPU 观感                |
| Q5  | **度量升级（可选）**          | ROI 掩膜（忽略 HUD/粒子）；SSIM 或 FLIP 辅 RMSE                                   | 1px 对齐偏移不再轻易假红；不取消 `--approve`                        |




### 广（Q1–Q2 之后）


| 序   | 项                   | 做法                                              | 验收                                       |
| --- | ------------------- | ----------------------------------------------- | ---------------------------------------- |
| C1  | **Validation 进 CI** | Debug：D3D12/Vulkan 校验层报错 → FAIL                 | `ci_headless` 可选 `-Validation`；无校验层 SKIP |
| C2  | **learn 小场景黄金图**    | `06_rhi_triangle`、`09_pbr_ibl` 各 1 条（确定性预设）     | 比再截一张满屏 Sandbox 更稳                       |
| C3  | **矩阵格升级为比图**        | `run_matrix_smoke.py` 对抽样格 `capture` + 基线（命令已有） | **已落地**：D3D12 默认 / TAA off / 阴影 off 三格        |
| C4  | **双后端一致性**          | 同场景同预设 D3D12 vs VK 比 SSIM/RMSE（无「绝对正确」基线）       | **已落地**：`run_backend_parity.py`（默认 Q5 ROI；松闸 RMSE≤90，现≈74 PASS）；`--strict` / CI `-StrictParity` 紧闸 48（现仍 FAIL，可选） |
| C5  | **语义断言**            | 平均亮度区间；TAA on≠off；阴影 on 更暗                      | **已落地**：`[gpu_headless][semantic]`；不替代黄金图                |
| C6  | **着色器字节码哈希**        | DXIL/SPIR-V 无意变更可检出                             | **已落地**：`check_shader_hashes.py` + `shader_hashes.json`（含 skybox）；进 `-Golden` |
| C7  | **加载 fuzz（可选）**     | glTF/序列化畸形输入不崩、路径不逃逸                            | **已落地**：`test_c7_load_fuzz.cpp` unit |




### 明确不做（本策略）

- 扩 Harness 命令、扩 MCP、CI 依赖 MCP  
- 自研 PIX/命令流 diff（TOOLING）  
- **RenderDoc / PIX 进 CI 门禁，或当自动化测试框架**：Python/`renderdoccmd` 可脚本化抓帧，但捕获大、版本/驱动敏感，和黄金图+Validation 重叠。**本机排错、失败时附 `.rdc` 可以**；不拿 Draw 遍历当回归套件。像素/GBuffer 用引擎 Readback（Q3）；屏障用 C1。  
- 全组合特性矩阵、每张 GPU 一张黄金图  
- 无确定性预设就把 RMSE 收到接近 0（只会假红）



### 实施顺序

```text
Q1 确定性截帧 → Q2 VK 真读回 → C1 Validation CI
  → C2 learn 小场景 + Q3 中间缓冲 → C3 矩阵比图
  →（可选）Q4 WARP / Q5 SSIM·FLIP / C4 双后端一致性 / C5–C7
```

不单开渲染里程碑；作为 **测试加深轨** 排入看板（[DOING_UNDO_TODO.md](DOING_UNDO_TODO.md)）。缺口登记：[KNOWN_GAPS.md](KNOWN_GAPS.md) **T03**。

## 4. 里程碑



### 4.1 基础段


| 里程碑               | 目标    | 主要交付                                                                                                          | 验收要点                                 |
| ----------------- | ----- | ------------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| **M1** 骨架         | 工程与接口 | CMake、Application/Module、core、**Win32 Window**、Input、RHI 工厂、**D3D12 清屏**、D3D11/GL/GLES stub、Vulkan 注册位；Catch2 | Win 清屏；键鼠可读；`ctest -R unit`          |
| **M2** D3D12 主路径  | 稳定绘制  | 资源/PSO/深度/Fence/上传/Resize；**shader_compile 可用**                                                               | 三角/纹理；Resize 不崩；着色器走工具链              |
| **M3** Graph + 异步 | 管线骨架  | FrameGraph、Compute、Readback、**异步加载+Pump 回调**、**Handle/引用骨架**、**Manifest/依赖约定**；CH06/CH11                      | FG 跑通；加载完成仅 Pump 后可见；缺依赖可诊断          |
| **M4** 场景·外设·调试   | 世界与操控 | Scene/Camera/剔除/Debug；手柄+ActionMap；**RenderScene SoA 抽取**                                                     | 键鼠/手柄转相机；渲染不写权威树                     |
| **M5** 环境与阴影      | 视觉基础  | Environment/PBR/IBL/CSM；**ibl_baker**；08–11                                                                   | IBL/CSM 可用；烘焙可复现                     |
| **M6** 角色与档位      | 完整感   | 蒙皮、探针、后处理基础、质量档                                                                                               | 蒙皮；档位差异                              |
| **M7** 特效·超分·音视频  | 动态媒体  | VFX、MV、DLSS+fallback、**D3D12VA**（Vulkan Video 在 M17）、Audio                                                    | D3D12 上视频/音频可验；VA 失败可诊断              |
| **M8** RT 与工具化    | 先进特性  | DXR+降级、控制台、序列化、GI、ActionMap 存盘；**ImGui 调试 UI 雏形**；**CPU/GPU Profiler**；**lightmap_baker**（可简陋）                | RT 可关；场景可存读；Lightmap 路径可复现；Pass 耗时可见 |
| **M9** 基础段验收      | 打磨    | Sandbox、回归、文档；**黄金图 v0**；**cook 清单+依赖图+可选打包**；纹理压缩路径可用                                                        | §1.1 走查；黄金图可跑；发版资产可复现                |




### 4.2 通用补强 + 物理（P0 → P1）


| 里程碑                   | 目标     | 主要交付                                                      | 验收要点                       |
| --------------------- | ------ | --------------------------------------------------------- | -------------------------- |
| **M10** 可见性·LOD·实例·流式 | P0 场景侧 | LOD、实例化/合批、遮挡剔除、流式+内存预算（**与引用计数协同**）                      | 大场景 Demo：流式不爆内存；有引用不被误淘汰   |
| **M11** 阴影·AA·AO·透明   | P0 画质侧 | 点/聚光阴影 Atlas、TAA、SSAO/GTAO、透明策略落地                         | 多灯阴影可见；TAA/AO 可开关；透明有文档+样例 |
| **M12** 物理            | 交互基础   | Jolt 封装、刚体/触发器/查询、角色控制器、碰撞 DebugDraw                      | 堆箱子+射线拾取+可行走角色             |
| **M13** P1 后处理与反射     | 画质标配   | SSR、DoF、运动模糊、自动曝光、体积雾、动态反射                                | Sandbox 室内外可感知             |
| **M14** P1 提交与显示      | 规模与输出  | Morph、扩展着色、间接绘制/GPU Cull、Bindless、**多线程命令录制**、HDR 输出、色彩管理 | §1.2 渲染项走查；并行录制可开关且稳定      |




### 4.3 UI


| 里程碑           | 目标          | 主要交付                                                           | 验收要点                             |
| ------------- | ----------- | -------------------------------------------------------------- | -------------------------------- |
| **M15** UI 完整 | 调试 + 运行时 UI | ImGui 工具链打磨；保留模式 HUD/菜单；字体/DPI；输入捕获；UI Pass；Sandbox 主菜单+HUD 示例 | 可键鼠操作菜单；游戏态 HUD；ImGui 与 3D 输入不打架 |




### 4.4 2D / 像素 / 混合渲染


| 里程碑              | 目标   | 主要交付                                          | 验收要点                      |
| ---------------- | ---- | --------------------------------------------- | ------------------------- |
| **M16** 2D·像素·混合 | 渲染能力 | Sprite/…；**图集格式约定**；**Tiled 导入**；Sandbox 混合场景 | 像素排序正确；Tilemap 可见；整数缩放无糊边 |




### 4.5 后端与 Linux


| 里程碑                     | 目标   | 主要交付                                                                                         | 验收要点                                                                      |
| ----------------------- | ---- | -------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| **M17** Vulkan（Windows） | 第二后端 | Vulkan Device/Swapchain/主路径对齐；SPIR-V；Validation；**Vulkan Video 视频纹理**与 D3D12VA 对等（能力不足 SKIP） | Win 上 `--backend=vulkan` 跑通同类 Sample（含视频若 Feature 支持；DXR 可暂 NotSupported） |
| **M18** Linux + Vulkan  | 跨平台  | Linux 窗口（X11 必做，Wayland 目标内）；Input 适配；Vulkan 全路径含 **Vulkan Video**；CMake/打包说明                | Linux 上 Vulkan 主 Sample + 视频（能力不足 SKIP）；无 D3D12 依赖                        |




### 4.6 网络


| 里程碑         | 目标   | 主要交付                                                                                                         | 验收要点                                                       |
| ----------- | ---- | ------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------- |
| **M19** 网络层 | 传输能力 | `engine/net`：HttpClient、WebSocket、Quic 可靠流；cpp-httplib / IXWebSocket / MsQuic 封装；TLS；`Net.Pump`；Loopback 集成测 | HTTP GET/POST 可测；WS 回显；QUIC 可靠收发；失败可诊断；Sandbox 可选遥测/资源拉取示例 |




### 4.7 引擎缺口补齐（P2）


| 里程碑                | 目标         | 主要交付                                                         | 验收要点                                        |
| ------------------ | ---------- | ------------------------------------------------------------ | ------------------------------------------- |
| **M20** 混合打磨       | 2D·3D 一体观感 | 精灵收 3D 影；2D MV；分层后处理；统一拣选/高亮；像素多 DPI 规则                      | 混合场景阴影与 TAA/超分不明显鬼影；拣选 2D/3D 一致；多 DPI 文档+样例 |
| **M21** 2D 深度      | 内容产能       | Tilemap chunk 流式；2D 骨骼（抽象+默认适配）；动画瓦片；2D 光雾；世界文字；相机特效；2D 粒子合批 | 大地图可流式；骨骼精灵可播；光雾/文字/震屏可开关                   |
| **M22** 动态 GI      | 室内外观感      | DDGI 或动态探针体积；与 Lightmap/探针共存策略；质量档可关                         | 动态物体间接光可感知；关 GI 可跑；两后端或明确仅 D3D12/VK         |
| **M23** 场景专题       | 开放世界起步     | 高度图地形+LOD；基础水体；实例化植被基础；Streaming 与内存预算衔接                     | Sandbox 可走地形水面植被小关；质量档可控                    |
| **M24** GPU Driven | 规模         | Mesh Shader 或增强 GPU Cull/间接；Feature 门控                       | 支持硬件上吞吐对比可测；不支持则光栅回退                        |
| **M25** 光追对齐       | API 对等     | Vulkan Ray Tracing 对齐 D3D12 示范路径；文档能力差                       | `--backend=vulkan` 可开关 RT 示范（驱动不足 SKIP）     |


说明：

- M17/M18 可与 M10–M16 **部分并行**（建议 D3D12 主路径稳定后再追平 Vulkan）；P0/P1 新特性须两后端落地或标注「仅 D3D12」。  
- **M19** 与图形无关，可与 M10–M18 并行（建议 M1 主循环稳定后开分支）。  
- **M20–M25** 建议在 M16（及尽量 M17）之后推进；M22–M24 可与 M21 部分并行。



## 5. 文档与代码同步节奏


| 阶段      | 文档动作                                                                                                                               |
| ------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| 当前      | 文档体系可执行：总索引、M1 清单、ADR 0001–**0028**、HOST_API/PREFAB、[LAYERS](../../docs/LAYERS.md)、game_kit / genre_kits / games / editor 规格、工具链绑定 |
| 每完成一里程碑 | 更新进度表；补 Sample/章节；必要时 ADR                                                                                                          |
| M1      | 按 GETTING_STARTED_M1 落地；回写 README 编译命令                                                                                             |
| M2      | 着色器编译工具说明                                                                                                                          |
| M5      | IBL baker 输入输出契约                                                                                                                   |
| M8–M9   | Lightmap baker、cook/清单、黄金图脚本；基础段二次扩展指南                                                                                             |
| M14     | 通用渲染能力矩阵、物理 API                                                                                                                    |
| M15     | UI API、输入路由约定、主题/字体规范                                                                                                              |
| M16     | 图集约定与 Tiled 导入；2D/像素/混合渲染 API 与排序约定                                                                                                |
| M17     | Vulkan Windows（含 Vulkan Video）与能力差（DXR 等）                                                                                          |
| M18     | Linux 构建与窗口后端说明 → **[LINUX.md](LINUX.md)**（`ENGINE_LINUX_VK`；运行时冒烟视 CI）                                                                                                                    |
| M19     | 网络 API（HTTP/WS/QUIC）与 TLS/线程约定                                                                                                     |
| M20–M21 | 混合/2D 深度 API 与拣选约定                                                                                                                 |
| M22–M23 | 动态 GI 与地形/水体/植被模块说明                                                                                                                |
| M24–M25 | GPU Driven / RT 能力矩阵                                                                                                               |
| 持续      | 按 [TESTING.md](TESTING.md) 扩展用例与 CI；测试加深按 **§3.1**                                                                                 |




## 6. 进度表


| 里程碑                     | 状态                                                                                        |
| ----------------------- | ----------------------------------------------------------------------------------------- |
| **M1** 清屏 + 单测          | **完成**（D3D12 清屏 Sample + unit 可跑；单测暂用内置 harness，Catch2 待网络）                               |
| **M2** D3D12 主路径        | **完成**（DXC `shader_compile`、深度/上传/PSO、纹理三角 `sample_02_triangle`、Resize）                   |
| **M3** Graph + 异步       | **完成（加深）**：Pump + `DispatchCompute` + **D3D12 真 Readback**；Validation 可选 |
| **M4** 场景·外设·调试         | **完成（加深）**：Win32 键鼠→InputSystem、Escape 关闭、RenderScene Extract                             |
| **M5** 环境与阴影            | **100%（Win）**：CSM Poisson/tile clamp/法线 bias；IBL；baker                                      |
| **M6** 角色与档位            | **100%（验收）**：glTF joints→SkinVertexCpu；QualityTier 拉开 cascade/atlas/距离/植被/DoF            |
| **M7** 特效·超分·音视频        | **100%（本口径）**：TrailRibbon；分辨率缩放+Jitter；WAV；**VA stub 可诊断；真解另波**；FSR/DLSS 外置                   |
| **M8** RT 与工具化          | **100%（本口径）**：Profiler BeginPass + GPU timestamp 门控；Lightmap 运行时；DXR Feature 探测         |
| **M9** 基础段验收            | **产品可用**：Sandbox；gpu-headless；learn 阶梯                                                      |
| **M10** 可见性·LOD·实例·流式   | **100%（无 vendor）**：LodSelect；StreamingBudget；GPU Instanced + Cull CS                       |
| **M11** 阴影·AA·AO·透明     | **100%（验收）**：CSM；spot+point local；SSAO/TAA；透明距离排序                                         |
| **M12** 物理              | **100%（验收）**：Jolt 胶囊 + MoveCharacter ShapeCast；learn/25 WASD                             |
| **M13** P1 后处理与反射       | **100%**：后处理栈 + vignette/grain；反射探针 GPU                                                   |
| **M14** P1 提交与显示        | **100%（无 vendor）**：Morph；SubmitConfig；Indirect；Bindless Feature                           |
| **M15** UI 完整           | **100%（本口径）**：ImGui+Retained HUD；RmlUi 外置已接受                                              |
| **M16** 2D·像素·混合        | **100%（验收）**：多层 Tiled + collision；IntegerScale；learn/30                                 |
| **M17** Vulkan（Windows） | **100%（Win 矩阵）**                                                                           |
| **M18** Linux + Vulkan  | **文档+构建说明加深（运行时冒烟视 CI 机）**：见 [LINUX.md](LINUX.md)；`ENGINE_LINUX_VK`；X11 窗口/Wayland 后置                                                                             |
| **M19** 网络层             | **100%（本口径）**：HTTP；HTTPS 提示；WS loopback；QUIC ADR 0031 SKIP                               |
| **M20** 混合打磨            | **100%**：Pick + MIXED_PICK.md；IntegerScale                                                   |
| **M21** 2D 深度           | **100%（加深）**：Tilemap→Sprite；SkeletonClip2D；雾/BMFont/震屏                                     |
| **M22** 动态 GI           | **100%（本口径）**：ProbeVolume + Lightmap 共存（非 DDGI）                                            |
| **M23** 场景专题            | **100%（加深）**：地形/水面/植被；QualityTier 密度                                                     |
| **M24** GPU Driven      | **100%**：HiZ + Cull；MeshShader Feature SKIP（C08）                                           |
| **M25** 光追对齐            | **100%（本口径）**：DXR 硬件探测 + ADR 0030；VK RT SKIP；W4 stub dispatch contract |
| **M26** Forward+ / P3    | **已开簇（ADR 0032）**：C01/C02/C10/C04/C16/C20 落地；其余 §4 后置                                    |
| **M27+** W4–W6 加深       | **已收口（ADR 0033）**：画质债 / Linux·HTTPS·VA 文档 / GI·水面·混合·GPU 蒙皮 stub·Meshlet 门控·PSO 热更请求 |
| **M27+** W7 对标加深       | **已收口（ADR 0034）**：C05 云雾 · C03 IES · C14 世界字 · C04 色差 · C12 D3D12 CS 蒙皮 · DXR 真 DispatchRays |
| **M27+** Mega-W8            | **已收口（ADR 0035）**：tile 灯/meshlet/VT/MsQuic/天气·FFT海·浮力/动画2D/VK蒙皮·热更/GK·ED 冒烟 |


> 无 vendor 100% 口径见看板；测试门禁：`ci_headless.ps1 -Golden`（Q1+Q3+C2+C3+C6）+ matrix 比图 + C4 薄对标（默认记回归）；可选 `-Validation`（C1）。**Harness 冻结；MCP 不进门禁。** §3.1：**Q1–Q3 / C1–C6 已落地（C4 记对标）**；Q4/Q5 / 严 C4 仍后置。



## 7. 建议实施顺序（M1 内）

可执行清单（目录、CMake、验收命令）：**[GETTING_STARTED_M1.md](GETTING_STARTED_M1.md)**。

1. CMake 与目录骨架
2. `core` → `platform` → `input` 骨架
3. RHI + D3D12 清屏（**M1 可不强制 shader_compile**；工具链从 M2 强制，见 TOOLING）
4. Application 主循环
5. **tests/unit + Catch2 + CTest**
6. 更新 README 编译运行与跑测说明



## 8. 相关文档

- [README.md](README.md) — 文档总索引  
- [DOING_UNDO_TODO.md](DOING_UNDO_TODO.md) — **当前迭代 Doing / Undo / Todo**  
- [../../docs/LAYERS.md](../../docs/LAYERS.md) — **工作区分层权威**（`editor/` 规格不在本目录）  
- [../../game_kit/docs/PLAN.md](../../game_kit/docs/PLAN.md) — **游戏可用主缺口 GK0–GK3**（见 §1.9）  
- [GETTING_STARTED_M1.md](GETTING_STARTED_M1.md) — **M1 可执行清单**  
- [HOSTING.md](HOSTING.md) — **玩法层 / 脚本外挂**；独立编辑器不在本树  
- [HOST_API.md](HOST_API.md) — **Host API v0**  
- [PREFAB_SCHEMA.md](PREFAB_SCHEMA.md) — **场景/Prefab schema**  
- [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md) — **Cook/异步/线程分离/寿命/数据依赖与生命周期/GPU Profiling**  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [POSITIONING.md](POSITIONING.md)  
- [TOOLING.md](TOOLING.md) — **工具链必要/可后置/不做**  
- [STANDARDS.md](STANDARDS.md) — **编码 / 架构 / 模块通讯 / §15 双后端分级与 SoA**  
- [KNOWN_GAPS.md](KNOWN_GAPS.md) — **缺口 ↔ M20–M25**；**§4 含 C19–C21**  
- [learn/adr/0023-engine-gap-fill-m20-m25.md](learn/adr/0023-engine-gap-fill-m20-m25.md)  
- [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md) — 调试/调优/排错  
- [THIRD_PARTY.md](THIRD_PARTY.md) — **可引入的第三方库清单**  
- [TESTING.md](TESTING.md) — **单测 / 集成 / 自动化测试**（§8 分工与覆盖）  
- [SANDBOX_MCP.md](SANDBOX_MCP.md) — **Harness 保留 / MCP 冻结**  
- [learn/README.md](learn/README.md)  
- [learn/PATH.md](learn/PATH.md)

