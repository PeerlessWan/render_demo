# games

> 具体游戏工程目录。内容、数值、关卡与发行物放这里；可选用 0..N 个 [`genre_kits`](../genre_kits/)。

权威分层：[../docs/LAYERS.md](../docs/LAYERS.md)。

## 约定

```text
games/
├── README.md
└── <title>/                 # 例：pixel_rpg、fps_proto
    ├── README.md            # 依赖哪些 kit / 引擎里程碑
    ├── docs/                # 可选：该作范围与缺口
    └── …                    # 实现后
```

## 依赖方向

```text
games/<title>  →  genre_kit?  →  game_kit  →  render_engine
```

- 可以不依赖任何 genre kit（玩法全写在本工程）。  
- 禁止直链 `render_engine` backends；禁止反向被 kit 依赖。

## 抽 kit 时机

同品类逻辑在第二个标题出现，或明确要开源/复用时，再迁到 `genre_kits/<name>_kit`。过早抽象不做硬性要求。

## 现状

占位目录；尚无游戏工程。先推进 `render_engine` M1 与 `game_kit` 文档/实现。
