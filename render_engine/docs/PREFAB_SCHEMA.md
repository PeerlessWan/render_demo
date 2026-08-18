# Prefab / 场景共享 Schema（冻结 v3）

> 跨 `render_engine`（World 组件）、`game_kit`（序列化 / 脚本）、`editor`（编辑存盘）的 **同一套数据约定**。  
> **状态：冻结（format_version = 3）**。读者须拒绝不支持的主版本。v1 / v2 仍可加载。

## 1. 所有权

| 数据 | 权威 | 其它消费者 |
|---|---|---|
| 节点层级、Transform、Mesh / Light / Camera / Collider / Sprite | `engine::scene::World` | editor 编辑；game_kit Capture/Apply |
| 脚本路径、公开字段（`--@export`） | game_kit | editor Inspector |
| Manifest / 依赖图 | cook `manifest.json` | editor Lint 面板 |
| Prefab 实例 override | 节点 `override` JSON | Apply 写回源 Prefab |

未知 `components[].type` 与未知字段必须 round-trip 保留。

## 2. 场景文件

```text
SceneDocument
  format_version: 3
  host_api_hint?: string
  nodes: Node[]
  extensions?: object
```

### Node

| 字段 | 说明 |
|---|---|
| `id` | 稳定字符串 ID |
| `name` | 显示名 |
| `parent` | 父 id 或 null |
| `transform` | `{t,r,s}` 局部 TRS |
| `visible` | bool |
| `prefab_id` | 若此节点是 Prefab 实例 |
| `script_path` | v1 别名；v3 以 Script 组件为准 |
| `components` | 见下 |
| `extra` | 编辑器旁路（须保留） |
| `override` | 实例属性覆盖（t/x/y/z/sx/visible/fields/material/light） |

### 组件

| type | 载荷 |
|---|---|
| `MeshRenderer` | `mesh`, `material` / `materials[]` |
| `Light` | `kind` (0 point / 1 spot / 2 dir), `range`, `intensity`, `color` `[r,g,b]` |
| `Camera` | `active`, `fovy` |
| `Collider` | `hx`, `hy`, `hz` |
| `Sprite` / `Tilemap` | `atlas`, `gid` |
| `Script` | `script` 路径, `fields` 对象 |
| `GameTag` | `script` 存标签名 |

## 3. Prefab

```text
PrefabDocument = SceneDocument + prefab_id（根节点与 extensions.game_kit）
```

- **实例化：** 深拷贝子树，新 NodeId；根记录 `prefab_id`。  
- **override：** 实例 `override` 合并到变换/显隐；`MergeOverrideJson`。  
- **Apply：** 把实例 TRS/组件写回源 Prefab 文件。  
- **嵌套：** 子节点 `prefab_id` 指向 catalog 中另一 Prefab 时 `InstantiateNested` 再实例化。

## 4. 版本

| 规则 | 说明 |
|---|---|
| `format_version` | 1、2、3 可读；Capture 写 **3** |
| 未知组件 | 保留 extra JSON，不删除 |
| v1 `script_path` | 加载后可提升为 Script 组件 |

## 5. 相关

- [HOST_API.md](HOST_API.md)  
- [../../game_kit/include/game_kit/scene_document.h](../../game_kit/include/game_kit/scene_document.h)  
- [../../editor/docs/GAPS.md](../../editor/docs/GAPS.md)
