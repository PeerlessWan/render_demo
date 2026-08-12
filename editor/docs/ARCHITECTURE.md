# editor 架构

## 1. 工作区位置

> 分层：[../../docs/LAYERS.md](../../docs/LAYERS.md)。

```text
render_demo/
├── render_engine/
├── game_kit/          # 可选：脚本组件 schema
├── genre_kits/        # 可选：品类元数据（编辑器一般不硬依赖）
├── games/             # 可选：打开某作内容工程
└── editor/            # 本工程
    ├── app/           # 编辑器进程入口
    ├── viewport/      # 引擎 Application / 视口封装
    ├── ui/            # 工具 UI（ImGui 或自研）
    ├── editing/       # 选中、Gizmo、Undo、属性
    ├── io/            # 场景/Prefab 读写
    └── docs/
```

## 2. 逻辑分层

```text
┌──────────────────────────────────────────┐
│  Editor UI（层级树、检视器、内容浏览器）      │
├──────────────────────────────────────────┤
│  Editing Core                              │
│   Selection · Gizmo · UndoStack · Dirty    │
├──────────────────────────────────────────┤
│  Viewport Host                             │
│   创建/驱动 render_engine Application       │
│   EditorCamera · Picking · DebugDraw       │
├──────────────────────────────────────────┤
│  IO                                        │
│   Scene/Prefab ↔ 磁盘；触发 cook（可选）     │
└───────────────────┬──────────────────────┘
                    ▼
             render_engine 公开 API
        （可选）game_kit schema / 脚本路径
```

## 3. 进程模型（须在实现前 ADR 选定）

| 模式 | 优点 | 代价 |
|---|---|---|
| **A. 同进程** | 调试快、共享 Device | 寿命/热重载复杂；脚本崩溃需隔离 |
| **B. 分进程视口** | 稳；Runtime 挂了编辑器还在 | IPC、双进程资产 |

**文档默认推荐：** 一期 **同进程**（模式 A）MVP；发版可评估 B。选定后写入 `editor` 专用 ADR。

## 4. 与引擎帧循环

编辑器拥有自己的 `Application` 或嵌入循环。**Play 模式复用引擎权威帧序**（HOSTING / ARCHITECTURE §4.1）；**编辑态**为变体：

```text
Platform.PumpEvents
Input.PollAndDispatch
Net.Pump                    # 与引擎权威序一致；编辑态可空转
Asset.PumpAsync
  → EditorUI（工具面板；可与 Update 交错，但不得晚于 Extract）
  → 若 Play：game_kit Module.OnUpdate（及选用的 genre kit）
  → Video/Audio（若启用）→ Physics → World/Animation
  → 否则：跳过玩法；仅编辑相机控制
RenderScene.Extract → Render（含选中高亮 DebugDraw）
```

- **编辑相机** 与 **游戏相机** 分离。  
- Play-in-Editor：进入时快照，退出时恢复（或丢弃运行时脏数据）。

## 5. 数据流

共享 schema：[PREFAB_SCHEMA.md](../../render_engine/docs/PREFAB_SCHEMA.md)。Host API：[HOST_API.md](../../render_engine/docs/HOST_API.md)。

1. 打开：读场景 JSON/二进制 + Manifest。  
2. 编辑：改 Node 变换/组件属性 → Dirty。  
3. 保存：写回场景（保留未知扩展块）；更新依赖边；可选调用 cook / C20 校验。  
4. 运行时加载：与 Sandbox/game 同一套 IO。

## 6. 目标目录（代码未开始）

```text
editor/
├── CMakeLists.txt
├── app/
├── viewport/
├── ui/
├── editing/
├── io/
├── resources/          # 编辑器自身图标等
└── docs/
```

## 7. 相关

- [FEATURES.md](FEATURES.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../render_engine/docs/HOSTING.md](../../render_engine/docs/HOSTING.md)  
- [../../render_engine/docs/RUNTIME_FOUNDATIONS.md](../../render_engine/docs/RUNTIME_FOUNDATIONS.md)  
