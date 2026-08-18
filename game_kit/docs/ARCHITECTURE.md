# game_kit 架构

## 1. 在工作区中的位置

权威分层：[../../docs/LAYERS.md](../../docs/LAYERS.md)。

```text
render_demo/
├── render_engine/     # Device、Scene、Render、Physics、UI、Assets…
├── game_kit/          # 本层：品类无关 Gameplay + Script
│   ├── runtime/       # 目标结构
│   ├── script/        # VM 封装 + 绑定
│   ├── samples/       # 通用可玩 Demo
│   └── docs/
├── genre_kits/        # 可选品类层（依赖本层，不反向）
├── games/             # 具体游戏（可选依赖 genre kit）
└── editor/            # 可选；可读 Prefab/脚本元数据
```

依赖方向：**只向下** → `render_engine` 公开头。禁止依赖 `backends/`、FrameGraph/三方头。  
禁止 `game_kit` 依赖 `genre_kits/*` 或 `games/*`。

## 2. 逻辑分层

```text
┌────────────────────────────────────────┐
│  games/<title> 内容与该作逻辑            │
├────────────────────────────────────────┤
│  genre_kits/*（可选品类玩法）            │
├────────────────────────────────────────┤
│  Script VM + Bindings（Lua 等）         │
├────────────────────────────────────────┤
│  Gameplay Runtime（本层，品类无关）      │
│   LevelFlow · Entity/ScriptComponent    │
│   Timer · EventBus · SaveGame 槽        │
│   Trigger 约定 · 玩家控制器              │
│   AnimPlayer · Mixer · Nav · Timeline   │
│   WorldSnapshot · LoopbackReplicator    │
├────────────────────────────────────────┤
│  Engine Host Adapter                    │
│   公开 API 薄稳定 Facade                │
└───────────────────┬────────────────────┘
                    │
             render_engine（公开 API）
```

## 3. 与引擎主循环的关系

默认以 **`IModule`** 挂入 `Application`。帧相位与引擎一致（**HOSTING / ARCHITECTURE 权威**）：

```text
Input → Net.Pump → Asset.PumpAsync
  → game_kit.OnUpdate   # 脚本 Tick、定时器、关卡逻辑
  → Video/Audio → Physics → World/Animation
  → RenderScene.Extract → Render
```

- 脚本默认 **主线程**；重活可线程池，回写 Scene 必须入队到 Update。  
- game_kit 提供 `GameKitScriptHost` 实现引擎 `IScriptHost`（C19）；默认引擎侧仍是 `NullScriptHost`。

## 4. 目录

```text
game_kit/
├── CMakeLists.txt
├── include/game_kit/…      # 公开头
├── runtime/
│   ├── level_flow.cpp
│   ├── entity.cpp
│   ├── timer.cpp
│   ├── event_bus.cpp
│   ├── save.cpp
│   ├── trigger.cpp
│   ├── player_controller.cpp
│   ├── anim_player.cpp
│   ├── audio_mixer.cpp
│   ├── nav.cpp
│   ├── timeline.cpp
│   ├── snapshot.cpp
│   ├── prefab.cpp
│   └── runtime.cpp
├── script/
│   ├── vm/lua_vm.cpp
│   ├── bindings/lua_api.cpp
│   └── …
├── samples/
└── docs/
```

## 5. 数据与序列化

场景格式对齐 [PREFAB_SCHEMA.md](../../render_engine/docs/PREFAB_SCHEMA.md)。

| 数据 | 所有者 | 格式约定 |
|---|---|---|
| 渲染场景 / 资源清单 | render_engine | 引擎场景序列化 + Manifest |
| Prefab（渲染+脚本挂点） | 约定共享；game_kit 挂接脚本 | 见 PREFAB_SCHEMA |
| 玩法存档 | game_kit 提供槽；品类 schema 在 genre kit / 游戏 | 与场景文件分离 |
| 脚本源文件 | game_kit | 文本；cook 时可进依赖图（可选） |

## 6. 相关

- [SCRIPTING.md](SCRIPTING.md)  
- [FEATURES.md](FEATURES.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../genre_kits/README.md](../../genre_kits/README.md)  
- [../../render_engine/docs/HOSTING.md](../../render_engine/docs/HOSTING.md)  
- [../../render_engine/docs/RUNTIME_FOUNDATIONS.md](../../render_engine/docs/RUNTIME_FOUNDATIONS.md)  
