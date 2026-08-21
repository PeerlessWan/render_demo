# editor 缺口

> 对标 **Godot / Unity 中小 PC 项目的编辑器**，不是 UE5。  
> **现状：** 摆放器 + 编辑器可用已收口。默认内容仍可走外部 DCC + CLI；编辑器内扫 `editor/content`。  
> 一期目标是 **关卡摆放器**（能摆、能改、能存、能再加载、能 Play），不是全能编辑器。  
> **ED-G10–G37：本尺子内已按代码验收收口**（`editor_smoke_tests` 全绿；无 ED-G31；§4 刻意不对齐除外）。  
> **对标 Godot 中小关卡体感 ≈95%**（见 [ENGINE_VS_GODOT_EDITOR.md](ENGINE_VS_GODOT_EDITOR.md)）；热重载受设备两槽天花板，不虚标 100%。

## 0. 三口径

| 口径 | 含义 | 本层状态 |
|---|---|---|
| **摆放器可用** | 打开场景、点选变换、场景树/检视、保存后 Runtime 可读 | **已收口 ≈100%** |
| **编辑器可用** | + Play-in-Editor、暂停、Prefab 放置、脚本字段 | **已收口 ≈95%** |
| **对标主流** | 材质图、UMG、蓝图、一站式导入、粒子时间轴… | **不宣称对齐**；见 §4 |

权威排期：[PLAN.md](PLAN.md)。功能 ID：[FEATURES.md](FEATURES.md)。自评：[ENGINE_VS_GODOT_EDITOR.md](ENGINE_VS_GODOT_EDITOR.md)。

---

## 1. 阻塞实现的决策债

| ID | 缺口 | 说明 | 对应 |
|---|---|---|---|
| ED-G01 | 无代码 | **已写 ED0–ED5**；`editor_smoke_tests` + `--headless` | ED0 |
| ED-G02 | 场景/Prefab schema 未冻结 | **已冻结 v3**：[PREFAB_SCHEMA.md](../../render_engine/docs/PREFAB_SCHEMA.md) | 引擎 M8；GK4；ED2–ED5 |
| ED-G03 | 进程模型未 ADR | **已锁定同进程**：[adr/0001-same-process.md](adr/0001-same-process.md) | ARCHITECTURE §3 |
| ED-G04 | 拣选 API 未单列 | 已包 `mixed::Pick` | ED2 |
| ED-G05 | Undo 模型未定 | **已选定命令栈** | ED2 |
| ED-G06 | Play 快照策略未定 | **退出恢复快照**（ADR 0001） | ED4 |
| ED-G08 | C20 CLI 未做 | **已闭环**：内置 SceneDocument v3 校验 + Manifest 依赖图；缺 `content_lint` 不掩盖场景错误 | 引擎 TOOLING |

---

## 2. 一期必补 ED-G10–G20 — **已收口**

> 下列在主流里是开箱能力；本层 **ED0–ED5 + Gizmo/Play 加深已写满**。

| ID | 项 | 主流对照 | 里程碑 | 状态 |
|---|---|---|---|---|
| ED-G10 | 进程空壳 | 能启动的 Editor 窗口 | ED0 | **100%** |
| ED-G11 | 3D/2D 视口 + 编辑相机 | 透视 + Top/Front/Side 正交；右键飞；中键平移 | ED1 / EDVP01–02 | **100%** |
| ED-G12 | 点选、Gizmo、选中高亮、Undo | Scale 立方体手柄；默认 mesh bounds；NodeSnap 含灯/碰撞/Sprite | ED2 / EDVP03–04、EDIO05 | **100%** |
| ED-G13 | 打开/保存场景 | 打开前 ClearWorld；同一套 SceneDocument v3 | ED2–ED3 / EDIO01 | **100%** |
| ED-G14 | 场景树、检视器、创建/删除节点 | Manifest mesh/材质 Combo；复制保留组件 meta | ED3 / EDHI01–03 | **100%** |
| ED-G15 | 内容浏览器、拖拽放置 | 递归扫描；视口松手射线落点；mesh/lua Output 提示 | ED3 / EDIO02–03 | **100%** |
| ED-G16 | 保存后可选 cook | 设置勾选 cook_on_save；结果进 Output | ED3 / EDIO04 | **100%** |
| ED-G17 | Play / 暂停 / 退出不脏（或可弃） | 物理世界 Enter 建一次；Pause/Space | ED4 / EDPL01–02 | **100%** |
| ED-G18 | 引擎调试叠加 | Profiler / 碰撞 / 网格 | ED4 / EDPL03 | **100%** |
| ED-G19 | Prefab **放置实例** | 含 override（ED-G32） | ED5 / EDHI05 | **100%** |
| ED-G20 | 脚本路径与公开字段 | `game_kit` `--@export`；fields 进 override | ED5 / EDHI04 | **100%** |

**验收水位：** 手改 JSON → 点选存盘；Godot 早期那种小关卡编辑器水位。仍不是 Unity Editor 全家桶。

---

## 3. 一期后加深 ED-G30–G37 — **已收口（含已知天花板）**

> （无 ED-G31 编号。）

| ID | 项 | 交付摘要 | 状态 |
|---|---|---|---|
| ED-G30 | 多视口 | Top/Front/Side 经 `ApplyPaneCamera` 正交；Persp 保留 | **100%** |
| ED-G32 | Prefab 源 vs 实例覆盖 | yaw/pitch/roll + fields；Apply 清 override；Revert 重建子树 | **100%** |
| ED-G33 | 动画状态机 / 曲线 | transitions 存盘；SampleCurve 可选驱动选中 Y；不绑骨骼 | **100%**（能力尺子内） |
| ED-G34 | 地形雕刻套件 | 视口笔刷；GUI 与 `ApplyOp` Undo/Redo 均 Upload+SyncStreamer | **100%** |
| ED-G35 | 2D Tilemap / 图集编辑 | 视口 PaintTile + streamer | **100%** |
| ED-G36 | 光照烘焙 UI | Bake/BakeNav→Output；空 navmesh Fail | **100%** |
| ED-G37 | 资源/脚本热重载 | 贴图按 stem→albedo 0/1；地形 slot2；Play 中 ReloadPath | **≈90%**（设备仅两 albedo 槽；gltf 热传未满） |

---

## 4. 刻意不对齐（不要当欠债追）

> 改定位须新 ADR。与 [POSITIONING.md](POSITIONING.md)、[CONSTRAINTS.md](CONSTRAINTS.md) 一致。  
> **不属于 ED-G10–G37。**

| ID | 项 | 态度 |
|---|---|---|
| ED-G07 | 材质 / Shader 节点图 | **不做**（引擎 G17） |
| ED-G40 | UMG / 可视化 UI 编辑器 | **不做** |
| ED-G41 | FBX / USD 一站式导入 | 外部转 glTF + cook |
| ED-G42 | 内置完整 DCC | **不替代 Blender** |
| ED-G43 | NavMesh / 联机同步编辑 | **运行时烘焙入口已写**；不做离线多边形编辑器、不做联机同步编辑 |
| ED-G44 | 粒子 / 时间轴 / 过场编辑器 | 未排期 |
| ED-G45 | 可视化脚本（蓝图级） | **不做**；脚本在 `game_kit` |
| ED-G46 | 包管理器 / 协作 / 版本控制套件 | **不做** |
| ED-G47 | 音频混音台 / DSP 编辑 | 引擎不做 DSP（ADR 0013） |

对标主流仍弱在：**材质图、UI 编辑、蓝图、导入管线、粒子/时间轴**。这是定位，不是漏排。

---

## 5. 依赖（本层补不了、要等别人）

| 依赖 | 谁 | 挡住 |
|---|---|---|
| 场景序列化 + Manifest | 引擎约 M8–M9 | ED1 打开场景 — **已具备** |
| 公开拣选 / DebugDraw | 引擎 M4 / M20 | ED2 — **已具备** |
| Host API / Prefab schema 冻结 | 引擎草案 | 存盘 round-trip — **v3 已冻** |
| Prefab + 脚本字段 | `game_kit` GK4 | ED5 — **已接线** |
| C20 CLI | 引擎 `tools/` | 可先于视口，非本层实现 — **已闭环** |

编辑器 **不是** 工作区「游戏可用」主缺口（那是 GK0–GK3）。无编辑器时 DCC + cook 仍可验收引擎。

## 相关

- [PLAN.md](PLAN.md)  
- [FEATURES.md](FEATURES.md)  
- [ENGINE_VS_GODOT_EDITOR.md](ENGINE_VS_GODOT_EDITOR.md)  
- [POSITIONING.md](POSITIONING.md)  
- [CONSTRAINTS.md](CONSTRAINTS.md)  
- [../../render_engine/docs/HOSTING.md](../../render_engine/docs/HOSTING.md)  
- [../../game_kit/docs/PLAN.md](../../game_kit/docs/PLAN.md)  
