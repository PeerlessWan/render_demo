# editor 功能清单

> `规划` = 目标能力；实现见 [PLAN.md](PLAN.md)。一期做「能摆、能改、能存、能再加载」。  
> 对标 Godot 中小关卡体感见 [ENGINE_VS_GODOT_EDITOR.md](ENGINE_VS_GODOT_EDITOR.md)（≈95%，不虚标全能 100%）。

## 1. 视口与场景

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDVP01 | 3D/2D 视口 | 3D 透视 + 2D 正交（Viewport「2D」：俯视 XZ、滚轮改 ortho_height、平面平移）；经引擎 Present | 已闭环 |
| EDVP02 | 编辑相机 | 右键观察；右键+WASD 飞；中键平移；滚轮缩放；与游戏相机分离 | 已闭环 |
| EDVP03 | 网格/Gizmo | 平移/旋转/缩放（Scale 轴端立方体）；轴长随距离；吸附可选 | 已闭环 |
| EDVP04 | 选中高亮 | DebugDraw mesh bounds（默认可关）；复用引擎拣选 | 已闭环 |
| EDVP05 | 多视口 | Split 四格；Top/Front/Side 走 `ApplyPaneCamera` 正交 | 已闭环 |

## 2. 层级与属性

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDHI01 | 场景树 | 显隐、重父级、搜索 | 已闭环（DnD 重父级 / 搜索 / 改名 / Ctrl 多选） |
| EDHI02 | 检视器 | 变换/灯/相机/碰撞/Sprite；网格+材质 Combo 来自 Manifest AssetId（不限 cube/ground） | 已闭环 |
| EDHI03 | 创建/删除节点 | Cube/Empty/Ground/Player/Light/Camera/Collider/Sprite | 已闭环 |
| EDHI04 | 脚本组件字段 | `--@export` 注入 Lua 全局 + persist | 已闭环 |
| EDHI05 | Prefab 编辑 | Place=`InstantiateNested`；override 覆盖 TRS(含旋转)/visible/material/light/fields；Apply 写回并清 override；Revert 重建子树 | 已闭环 |

## 3. 内容与 IO

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDIO01 | 打开/保存场景 | SceneDocument v3；`extensions.editor` 存 heights/tiles/atlas/anim | 已闭环 |
| EDIO02 | 内容浏览器 | 递归扫描；png/jpg 解码预览；json/gltf/lua 稳定色块 | 已闭环 |
| EDIO03 | 拖拽放置 | Content 松手射线落点；mesh/脚本无行为时 Output 提示 | 已闭环 |
| EDIO04 | 调用 cook | 保存后可选 cook_on_save；结果进 Output | 已闭环 |
| EDIO05 | Undo/Redo | 变换/节点/组件；地形与 Tile 在 GUI 与 `ApplyOp` 路径均 Upload+SyncStreamer | 已闭环 |

## 4. 播放与调试

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDPL01 | Play-in-Editor | GUI/MCP 同一 `ApplyOp`；克隆 `play_world`；Stop 映回 edit meta；物理盒每帧跟随节点 | 已闭环 |
| EDPL02 | 暂停 | Pause/Step；快捷键 Space / Pause | 已闭环 |
| EDPL03 | 引擎调试叠加 | Profiler / 碰撞 / 网格 | 已闭环 |

## 5. 后置（ED-G30–G37）

| 项 | 说明 | 对应 |
|---|---|---|
| 多视口 | 四格；Top/Front/Side 正交 | ED-G30 **100%** |
| Prefab | 旋转+fields override；Apply/Revert 子树 | ED-G32 **100%** |
| 动画 | 状态/转移存盘；SampleCurve 可选驱动选中节点 Y | ED-G33 **100%**（尺子内） |
| 地形 | 视口笔刷；Undo（含 MCP）后 Upload+SyncStreamer | ED-G34 **100%** |
| Tilemap | 视口 PaintTile + streamer | ED-G35 **100%** |
| Bake | lightmap→Output；BakeNav 空 mesh Fail | ED-G36 **100%** |
| NavMesh 烘焙 | `editor_bake_nav` 入口；无离线多边形编辑 | ED-G43（非离线编辑器） |
| Lint | SceneDocument v3 + manifest 依赖图 | ED-G08 / EDIO |
| 热重载 | 贴图 albedo 0/1 两槽；地形 slot2；脚本 ReloadPath | ED-G37 **≈90%**（设备天花板） |

## 6. 明确不做（不要当欠债）

> [GAPS.md](GAPS.md) §4。改定位须新 ADR。

| 项 | 说明 |
|---|---|
| 材质 / Shader 节点图 | 范围外（引擎 G17） |
| UMG / 可视化 UI 编辑器 | 范围外 |
| FBX / USD 一站式导入 | 外部转 glTF + cook |
| 内置完整 DCC | 不替代 Blender |
| NavMesh / 联机同步编辑 | 一期不塞 |
| 粒子 / 时间轴 / 过场编辑器 | 未排期 |
| 可视化脚本（蓝图级） | 脚本在 `game_kit` |
| 包管理器 / 协作套件 | 不做 |
| 音频混音台 / DSP | 引擎不做 DSP |

轻量 CLI（仅校验/浏览）见引擎 **C20**，可先于本编辑器；**不是**本层文档职责。

## 7. 相关

- [PLAN.md](PLAN.md)  
- [GAPS.md](GAPS.md)  
- [POSITIONING.md](POSITIONING.md)  
