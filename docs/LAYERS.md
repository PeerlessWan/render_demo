# 工作区分层（权威）

> 后续要做**多种游戏类型**时，采用：**薄通用 `game_kit` + 按需品类 kit + 游戏工程**。  
> 本文是分层边界的权威说明；接入契约仍以 [render_engine/docs/HOSTING.md](../render_engine/docs/HOSTING.md) 为准。

## 1. 推荐结构

```text
render_demo/
├── render_engine/     # 渲染中台：RHI / 场景 / 物理 / UI / 音频播控 / 网络传输 …
├── game_kit/          # 品类无关：脚本 VM、关卡流、Entity、事件、存档槽、触发器骨架
├── genre_kits/        # 可选品类层（按需建仓/子目录，勿塞进 game_kit）
│   ├── rpg_kit/       # 例：对话 / 背包 / 任务 / 战斗骨架
│   ├── shooter_kit/   # 例：武器 / 命中 / 视角手感骨架
│   └── …              # 新品类另开 xxx_kit，不回填 game_kit / render_engine
├── games/             # 具体游戏：内容、数值、关卡；选用 0..N 个 genre kit
├── editor/            # 独立视口编辑器（可选）
└── tools/             # 离线工具过渡位 → 目标 render_engine/tools
```

对外叙事可称：

- **渲染中台** = `render_engine`
- **轻量游戏引擎壳** = `game_kit` + `render_engine`
- **可玩产品** = `games/<title>` ± `genre_kits/*` + 上两者

## 2. 各层职责

| 层 | 放什么 | 不放什么 |
|---|---|---|
| `render_engine` | 渲染与子系统中台、公开 Host API | 玩法规则、脚本 VM、品类专有名词、状态同步产品 |
| `game_kit` | 所有游戏都要的运行时骨架 + 脚本 | 「任务」「弹匣」「对话树」等品类逻辑；RHI/backends |
| `genre_kits/*` | 可跨多个同品类标题复用的玩法 | 具体关卡/数值/剧情；Device/Swapchain |
| `games/<title>` | 内容、配表、关卡脚本、品牌与发行 | 第二份引擎；直链 backends |
| `editor` | 视口选中、属性、存盘 | 材质节点图全家桶；替代 DCC |
| 中间件（可选，与品类 kit 平级） | 多游戏共用且非品类专属：如 `net_sync`、NavMesh、空间音频桥 | 塞进 `render_engine` 核心（须单独 ADR） |

## 3. 依赖方向（只允许向下）

```text
games ──► genre_kit(s) ──► game_kit ──► render_engine
                │              │
                └──────────────┴──► 仅公开头 / Host API
editor ──► render_engine（视口）；可读 Prefab/脚本元数据约定
```

禁止：

1. `render_engine` 依赖任何 kit / game  
2. `game_kit` 依赖某个 `genre_kit` 或某个 `games/*`  
3. 品类 kit / 游戏 `#include` 引擎 `backends/**` 或三方头（ADR 0017）  
4. 把品类逻辑「为了方便」回填进 `game_kit` 或引擎核心  

## 4. 落地原则

1. **先薄后抽**：第一个标题可把玩法先写在 `games/<title>`；出现第二个同品类标题、或明确要复用时，再抽到 `genre_kits/xxx_kit`。  
2. **game_kit 保持品类无关**：没有品类专有 API 名词；存档只提供槽位/序列化壳，不规定 RPG 队伍 schema。  
3. **新品类开新 kit**：不把射击塞进 RPG kit，也不把两者揉进加厚的单一 `game_kit`。  
4. **跨品类能力用中间件**：联机复制、导航网格、音频 DSP 桥等与品类无关时，另立模块文档，默认仍不进引擎核心。  
5. **不阻塞引擎 M1**：本分层是工作区约定；实现仍从 `render_engine` M1 起，见 [GETTING_STARTED_M1.md](../render_engine/docs/GETTING_STARTED_M1.md)。

## 5. 与已有文档的关系

| 文档 | 角色 |
|---|---|
| 本文 | **工作区分层权威** |
| [README.md](README.md) | 工作区文档总索引 |
| [HOSTING.md](../render_engine/docs/HOSTING.md) | 引擎对外挂怎么挂、帧相位、禁止依赖 |
| [game_kit/docs](../game_kit/docs/README.md) | 通用玩法壳规格 |
| [genre_kits/README.md](../genre_kits/README.md) | 品类 kit 索引与建仓约定 |
| [games/README.md](../games/README.md) | 游戏工程约定 |
| [editor/docs](../editor/docs/README.md) | 视口编辑器 |
| ADR 0027 / **0028** | 宿主边界；品类分层决策 |

## 6. 相关 ADR

- [0027 宿主分层：脚本与编辑器在引擎外](../render_engine/docs/learn/adr/0027-hosting-script-editor-boundary.md)  
- [0028 多品类：薄 game_kit + genre_kits](../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
