# genre_kits

> **可选品类玩法层**。挂在 [`game_kit`](../game_kit/) 之上，服务多个同类型游戏；**不是**第二份渲染引擎。

权威分层：[../docs/LAYERS.md](../docs/LAYERS.md)。

## 何时建

- 第二个同品类标题需要复用玩法；或  
- 已明确该品类会多做几个原型 / 产品。

第一个游戏可以把逻辑写在 `games/<title>`，稳定后再抽到此目录。

## 约定目录（按需创建）

```text
genre_kits/
├── README.md              # 本文
├── rpg_kit/               # 例
│   ├── README.md
│   ├── docs/POSITIONING.md
│   └── …                  # 实现后
├── shooter_kit/           # 例
│   └── …
└── <name>_kit/
```

每个 kit 建议至少有：

| 文档 | 内容 |
|---|---|
| `README.md` | 一句话定位、依赖、状态 |
| `docs/POSITIONING.md` | 是/不是、与 game_kit 边界 |
| `docs/FEATURES.md` | 功能清单（规划/实现） |

## 依赖

- **必须**：`game_kit` 公开 API + `render_engine` Host API（通常经 game_kit 间接使用）  
- **禁止**：依赖某个具体 `games/<title>`；依赖引擎 backends；把通用关卡流/脚本 VM 再实现一份（应留在 game_kit）

## 示例职责（规划，非实现承诺）

| Kit | 可放 | 仍留给游戏 |
|---|---|---|
| `rpg_kit` | 对话运行时、背包/任务/战斗骨架、常用事件约定 | 剧情文本、数值表、具体地图 |
| `shooter_kit` | Hitscan/弹道壳、武器状态机骨架、射击相机辅助 | 枪械手感调参、关卡、联机规则 |
| 其他 | 按品类最小可复用集 | 内容与发行 |

## 中间件（与品类 kit 平级，勿混名）

多游戏、跨品类才需要的能力（如状态同步、NavMesh、空间音频桥）**不要**塞进某个 `*_kit` 名称里冒充品类包；另立模块并在 [LAYERS.md](../docs/LAYERS.md) 登记。

## 现状

仅约定与占位；**无代码**。不阻塞 `render_engine` M1 / `game_kit` GK0。
