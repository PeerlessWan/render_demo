# editor 功能清单

> `规划` = 目标能力；实现见 [PLAN.md](PLAN.md)。一期做「能摆、能改、能存、能再加载」。

## 1. 视口与场景

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDVP01 | 3D/2D 视口 | 3D 透视 + 2D 正交（Viewport「2D」：俯视 XZ、滚轮改 ortho_height、平面平移）；经引擎 Present | 已闭环 |
| EDVP02 | 编辑相机 | 右键观察；右键+WASD 飞；中键平移；滚轮缩放；与游戏相机分离 | 已闭环 |
| EDVP03 | 网格/Gizmo | 平移旋转缩放；轴长/命中随相机距离放大；吸附可选 | 已闭环（轴/环；Local/World；无轴时 Y 平面拖） |
| EDVP04 | 选中高亮 | 轮廓或 DebugDraw；复用引擎拣选能力（对齐 M20） | 已闭环 |
| EDVP05 | 多视口 | Split 勾选后同一帧 Persp/Top/Front/Side 四格提交；点击落到对应格 | 已闭环 |

## 2. 层级与属性

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDHI01 | 场景树 | 显隐、重父级、搜索 | 已闭环（DnD 重父级 / 搜索 / 改名 / Ctrl 多选） |
| EDHI02 | 检视器 | 变换（按轴改、松手 Undo）、灯光（kind/color/世界坐标）、相机 fovy、碰撞、Sprite 绘制与点选、材质 Combo 来自 Manifest AssetId | 已闭环 |
| EDHI03 | 创建/删除节点 | Cube/Empty/Ground/Player/Light/Camera/Collider/Sprite | 已闭环 |
| EDHI04 | 脚本组件字段 | `--@export` 注入 Lua 全局 + persist | 已闭环 |
| EDHI05 | Prefab 编辑 | Place=`InstantiateNested`；override 覆盖 TRS/visible/material/light/fields；Apply 写回源；Revert 清 override | 已闭环 |

## 3. 内容与 IO

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDIO01 | 打开/保存场景 | SceneDocument v3；`extensions.editor` 存 heights/tiles/atlas/anim | 已闭环 |
| EDIO02 | 内容浏览器 | png/jpg 解码预览；json/gltf/lua 固定可辨色（非随机 RGB） | 已闭环 |
| EDIO03 | 拖拽放置 | Content 拖进视口；Hierarchy DnD | 已闭环 |
| EDIO04 | 调用 cook | 保存后可选跑清单刷新 | 已闭环 |
| EDIO05 | Undo/Redo | 变换、创建删除、重父级（NodeId）、灯/碰撞/材质/字段/Sprite、地形、Tile、Anim | 已闭环 |

## 4. 播放与调试

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDPL01 | Play-in-Editor | GUI/MCP 同一 `ApplyOp`；克隆 `play_world`；Stop 映回 edit meta；物理盒每帧跟随节点 | 已闭环 |
| EDPL02 | 暂停 | Pause/Step 走 `ApplyOp` | 已闭环 |
| EDPL03 | 引擎调试叠加 | Profiler / 碰撞 / 网格 | 已闭环 |

## 5. 后置（对标主流仍弱、但本层已按中小编辑器闭环）

| 项 | 说明 |
|---|---|
| 多视口 | 同一帧四视口矩形 + 四相机（非 UE5 GPU RTT） |
| Prefab | schema 字段 override + Apply/Revert + 嵌套 Place |
| 动画 | 可增删状态/转移；4 键曲线 `SampleCurve` 预览；驱动选中 `AnimPlayer` |
| 地形 | Raise/Lower/Smooth；Undo；随场景存盘；视口 Upload |
| Tilemap | 图集路径 + GID；世界坐标投影为 ScreenQuad；Sprite/Tile 可见可点选 |
| Bake | 读当前场景灯 CPU 烘 lightmap 并 Upload；失败 isError |
| NavMesh 烘焙 | `editor_bake_nav`：`BakeFromWorld`；无离线多边形编辑 |
| Lint | 内置 SceneDocument v3 校验 + manifest 依赖图 |
| 热重载 | 贴图 UploadLitAlbedoRgba + 地形网格 Upload；脚本 ReloadPath |

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
