# editor 缺口

> 对标 **Godot / Unity 中小 PC 项目的编辑器**，不是 UE5。  
> **现状：** ED0–ED5 代码已写，Gizmo/Play/Content 已加深。默认内容路径仍是外部 DCC + CLI。  
> 一期目标是 **关卡摆放器**（能摆、能改、能存、能再加载、能 Play），不是全能编辑器。

## 0. 三口径

| 口径 | 含义 | 本层状态 |
|---|---|---|
| **摆放器可用** | 打开场景、点选变换、场景树/检视、保存后 Runtime 可读 | **已写已测**（ED0–ED3 + smoke） |
| **编辑器可用** | + Play-in-Editor、暂停、Prefab 放置、脚本字段 | **已写**（场景 Play + 体素 HUD） |
| **对标主流** | 材质图、UMG、动画工具、地形雕刻、一站式导入 | **不宣称对齐**；见 §3 / §4 |

权威排期：[PLAN.md](PLAN.md)。功能 ID：[FEATURES.md](FEATURES.md)。

---

## 1. 阻塞实现的决策债

| ID | 缺口 | 说明 | 对应 |
|---|---|---|---|
| ED-G01 | 无代码 | **已写 ED0–ED3**；测试闸门未开 | ED0 |
| ED-G02 | 场景/Prefab schema 未冻结 | 草案：[PREFAB_SCHEMA.md](../../render_engine/docs/PREFAB_SCHEMA.md)；摆放器已用 v1 JSON | 引擎 M8；GK4；ED2–ED5 |
| ED-G03 | 进程模型未 ADR | **已锁定同进程**：[adr/0001-same-process.md](adr/0001-same-process.md) | ARCHITECTURE §3 |
| ED-G04 | 拣选 API 未单列 | 已包 `mixed::Pick` | ED2 |
| ED-G05 | Undo 模型未定 | **已选定命令栈** | ED2 |
| ED-G06 | Play 快照策略未定 | **退出恢复快照**（ADR 0001） | ED4 |
| ED-G08 | C20 CLI 未做 | Manifest/场景校验、依赖图；**引擎 tools 候选**，可先于视口 | 引擎 TOOLING |

---

## 2. 一期必补（摆放器 → 小编辑器）

> 下列在主流里是开箱能力；本层 **ED0–ED5 + Gizmo/Play 加深已写**。  
> 建议 **GK3 之后** 再开 ED1–ED2；不挡 `game_kit` 游戏可用。

| ID | 缺什么 | 主流对照 | 里程碑 | FEATURES |
|---|---|---|---|---|
| ED-G10 | 进程空壳 | 能启动的 Editor 窗口 | ED0 | — |
| ED-G11 | 3D/2D 视口 + 编辑相机 | Scene 视口、飞行/轨道，与游戏相机分离 | ED1 | EDVP01–02 |
| ED-G12 | 点选、Gizmo、选中高亮、Undo | 移物体可撤销 | ED2 | EDVP03–04、EDIO05 |
| ED-G13 | 打开/保存场景 | 与 Runtime **同一套**格式 | ED2–ED3 | EDIO01 |
| ED-G14 | 场景树、检视器、创建/删除节点 | Hierarchy + Inspector | ED3 | EDHI01–03 |
| ED-G15 | 内容浏览器、拖拽放置 | Project 窗口拖进视口 | ED3 | EDIO02–03 |
| ED-G16 | 保存后可选 cook | 依赖图/清单刷新 | ED3 | EDIO04 |
| ED-G17 | Play / 暂停 / 退出不脏（或可弃） | Play-in-Editor | ED4 | EDPL01–02 |
| ED-G18 | 引擎调试叠加 | Profiler / 碰撞 / 像素网格开关 | ED4 | EDPL03 |
| ED-G19 | Prefab **放置实例** | 一期可不做完整 override | ED5 | EDHI05 |
| ED-G20 | 脚本路径与公开字段 | 有 `game_kit` 时；无则仍可编纯渲染场景 | ED5 | EDHI04 |

**验收水位：** ED3 完成 ≈ 手改 JSON 换成点选存盘；ED5 完成 ≈ Godot 早期那种小关卡编辑器，仍不是 Unity Editor。

---

## 3. 一期后仍弱（对标主流会差一截）

> 可另开里程碑 / ED6+；**不是**「忘了写进一期」。立项须改 PLAN + FEATURES 状态。

| ID | 缺口 | 说明 | 建议 |
|---|---|---|---|
| ED-G30 | 多视口 | 四视图等 | ED6 |
| ED-G31 | 吸附 / 批量 / 一键 cook | 网格对齐、多选改属性 | ED6 |
| ED-G32 | Prefab 源 vs 实例覆盖 | 一期只放实例；完整 override 后置 | ED5 加深 |
| ED-G33 | 动画状态机 / 曲线编辑器 | 引擎候选 C10 或上层自建；编辑器侧另议 | 后置 |
| ED-G34 | 地形雕刻套件 | 等引擎 M23 基础 | 另议 |
| ED-G35 | 2D Tilemap / 图集编辑 | 引擎有 Tilemap **渲染**导入；编辑器内刷瓦片未排期 | 后置 |
| ED-G36 | 光照烘焙 UI | Lightmap 走引擎 baker CLI；无编辑器内烘焙面板 | 后置 |
| ED-G37 | 资源/脚本热重载 | 引擎 C16；编辑器侧「保存即刷新」未排期 | 后置 |

---

## 4. 刻意不对齐（不要当欠债追）

> 改定位须新 ADR。与 [POSITIONING.md](POSITIONING.md)、[CONSTRAINTS.md](CONSTRAINTS.md) 一致。

| ID | 项 | 态度 |
|---|---|---|
| ED-G07 | 材质 / Shader 节点图 | **不做**（引擎 G17） |
| ED-G40 | UMG / 可视化 UI 编辑器 | **不做** |
| ED-G41 | FBX / USD 一站式导入 | 外部转 glTF + cook |
| ED-G42 | 内置完整 DCC | **不替代 Blender** |
| ED-G43 | NavMesh / 联机同步编辑 | 一期不塞；中间件另立 |
| ED-G44 | 粒子 / 时间轴 / 过场编辑器 | 未排期 |
| ED-G45 | 可视化脚本（蓝图级） | **不做**；脚本在 `game_kit` |
| ED-G46 | 包管理器 / 协作 / 版本控制套件 | **不做** |
| ED-G47 | 音频混音台 / DSP 编辑 | 引擎不做 DSP（ADR 0013） |

即使得完 ED5，对标主流仍弱在：**材质图、UI 编辑、动画工具、导入管线、地形雕刻**。这是定位，不是漏排。

---

## 5. 依赖（本层补不了、要等别人）

| 依赖 | 谁 | 挡住 |
|---|---|---|
| 场景序列化 + Manifest | 引擎约 M8–M9 | ED1 打开场景 |
| 公开拣选 / DebugDraw | 引擎 M4 / M20 | ED2 |
| Host API / Prefab schema 冻结 | 引擎草案 | 存盘 round-trip |
| Prefab + 脚本字段 | `game_kit` GK4 | ED5 |
| C20 CLI | 引擎 `tools/` | 可先于视口，非本层实现 |

编辑器 **不是** 工作区「游戏可用」主缺口（那是 GK0–GK3）。无编辑器时 DCC + cook 仍可验收引擎。

## 相关

- [PLAN.md](PLAN.md)  
- [FEATURES.md](FEATURES.md)  
- [POSITIONING.md](POSITIONING.md)  
- [CONSTRAINTS.md](CONSTRAINTS.md)  
- [../../render_engine/docs/HOSTING.md](../../render_engine/docs/HOSTING.md)  
- [../../game_kit/docs/PLAN.md](../../game_kit/docs/PLAN.md)  
