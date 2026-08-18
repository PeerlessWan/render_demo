# editor 功能清单

> `规划` = 目标能力；实现见 [PLAN.md](PLAN.md)。一期做「能摆、能改、能存、能再加载」。

## 1. 视口与场景

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDVP01 | 3D/2D 视口 | 经引擎 Present；质量档可降以保编辑流畅 | 已写 |
| EDVP02 | 编辑相机 | 飞行/轨道；与游戏相机分离 | 已写 |
| EDVP03 | 网格/Gizmo | 平移旋转缩放；吸附可选 | 已写（轴/环；Local/World；无轴时 Y 平面拖） |
| EDVP04 | 选中高亮 | 轮廓或 DebugDraw；复用引擎拣选能力（对齐 M20） | 已写 |
| EDVP05 | 多视口 | 2×2 pane；活动格驱动相机 | 已写 |

## 2. 层级与属性

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDHI01 | 场景树 | 显隐、重父级、搜索 | 已写（DnD 重父级 / 搜索 / 改名 / Ctrl 多选） |
| EDHI02 | 检视器 | 变换、灯光、相机、材质引用（AssetId） | 已写（World Light/Camera/Collider + 材质槽） |
| EDHI03 | 创建/删除节点 | Cube/Empty/Ground/Player/Light/Camera/Collider/Sprite | 已写 |
| EDHI04 | 脚本组件字段 | `--@export` + persist | 已写 |
| EDHI05 | Prefab 编辑 | 放置 + 属性 override + Apply 写回源 | 已写 |

## 3. 内容与 IO

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDIO01 | 打开/保存场景 | SceneDocument v3 | 已写 |
| EDIO02 | 内容浏览器 | Manifest/AssetId + 色块缩略图 | 已写 |
| EDIO03 | 拖拽放置 | Content 拖进视口；Hierarchy DnD | 已写 |
| EDIO04 | 调用 cook | 保存后可选跑清单刷新 | 已写 |
| EDIO05 | Undo/Redo | 变换/创建删除/属性/重父级 | 已写 |

## 4. 播放与调试

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| EDPL01 | Play-in-Editor | 克隆 World；退出不脏编辑场景 | 已写 |
| EDPL02 | 暂停 | 逻辑暂停 | 已写（含 Step） |
| EDPL03 | 引擎调试叠加 | Profiler / 碰撞 / 网格 | 已写 |

## 5. 后置（对标主流仍弱的产品切片）

| 项 | 说明 |
|---|---|
| 多视口 | 2×2 活动格；非 GPU RTT 四分屏 |
| Prefab | 属性 override + Apply 写回；嵌套 catalog |
| 动画 | 状态机拓扑 + 4 键曲线 |
| 地形 | 笔刷 + TerrainMesh 上传 |
| Tilemap | Streamer GID + Sprite 展开 |
| Bake/Lint | CLI + 依赖图 JSON |
| 热重载 | AssetHotReload + 脚本 Poll |

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
