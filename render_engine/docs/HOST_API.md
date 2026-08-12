# Host API v0 草案（公开契约面）

> 供 `game_kit` / `editor` / 外部游戏工程依赖。  
> **权威帧序：** [HOSTING.md](HOSTING.md) §4、[ARCHITECTURE.md](ARCHITECTURE.md) §4.1。  
> 状态：文档草案；随 M4–M9 实现冻结为 v0，破坏性变更走 semver / ADR。

## 1. 目标与稳定级别

| 级别 | 含义 |
|---|---|
| **Stable（目标 v0）** | 公开头符号；改签名须大版本或 ADR |
| **Preview** | 可用但不保证 ABI；文档标明 |
| **Internal** | `backends/`、匿名细节；宿主禁止依赖 |

宿主只准依赖 **Stable/Preview**，禁止 Internal（ADR 0017 / HOSTING §3）。

## 2. 公开头布局（实现时二选一，选定后写死）

**推荐：**

```text
include/engine/
  Application.hpp
  Module.hpp
  Result.hpp
  …
```

或 `engine/**/include` 汇总安装到同一前缀。CMake 导出 `render_engine::api`。

## 3. 模块与能力（v0 范围）

| 模块 | 宿主可用能力（摘要） | 最早里程碑 |
|---|---|---|
| **core** | `Result`、Log、Config、Clock、Math、Version | M1 |
| **application** | `Application`、`IModule`（Init/Update/Shutdown、Requires） | M1 |
| **platform** | 窗口尺寸/DPI/Resize 事件（只读查询） | M1 |
| **input** | ActionMap、轴/按钮状态、热插拔事件 | M1/M4 |
| **assets** | `AssetId`、`AssetHandle`、RequestLoad/Cancel、Pump 后完成回调、Manifest 查询 | M3 |
| **scene** | Node 层级、Transform、显隐、相机、渲染向序列化 IO | M4/M8 |
| **render** | 质量档开关、视图模式（调试）、**不**暴露后端命令录制 | M2+ |
| **physics** | Raycast/ShapeCast、Trigger 订阅、角色控制器接口 | M12 |
| **ui** | Retained：显隐/文本/基础控件；WantCapture 查询 | M8/M15 |
| **audio** | Clip 播放/停止/增益（无 DSP） | M7 |
| **net** | Http/Ws/Quic 请求与 Pump 回调 | M19 |
| **render2d / tilemap** | Sprite/Tilemap 创建与碰撞层数据只读导出 | M16 |
| **debug** | DebugDraw、Profiler 只读计数、控制台命令注册（受限） | M4/M8 |
| **feature** | `QueryFeature` / L0–L2 能力查询 | M1+ |

细节类型名以实现为准；本表约束 **能力边界**。

## 4. 禁止暴露（永远不是 Host API）

- `ID3D12*` / `Vk*` / 三方物理/UI 头  
- FrameGraph 内部 Compile 结构、描述符堆句柄  
- 直接创建 Device / Swapchain（由 Application 拥有）  
- 修改后端 PSO 字节码路径  

## 5. 回调与线程

1. 资产/网络完成回调：仅主线程、对应 `Pump` 之后。  
2. Module.OnUpdate：主线程。  
3. 宿主自建线程：回写 Scene 必须入队到 Update。  

## 6. 版本

| 版本 | 条件 |
|---|---|
| **0.x** | M4 起 Preview；M9 起宣称 Host API v0 Stable（核心子集） |
| 文档字段 | `engine.host_api_version` 启动日志打印 |

`game_kit` / `editor` 文档中声明依赖的最低 Host API 版本。

## 7. 相关

- [HOSTING.md](HOSTING.md)  
- [PREFAB_SCHEMA.md](PREFAB_SCHEMA.md)  
- [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md)  
- [../../game_kit/docs/CONSTRAINTS.md](../../game_kit/docs/CONSTRAINTS.md)  
- [../../editor/docs/CONSTRAINTS.md](../../editor/docs/CONSTRAINTS.md)  
