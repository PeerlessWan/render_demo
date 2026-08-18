# Prefab / 场景共享 Schema 草案

> 跨 `render_engine`（序列化/加载）、`game_kit`（脚本挂点）、`editor`（编辑存盘）的 **同一套数据约定**。  
> **状态：文档草案（D10）** — Mega-W9 **不冻结** Schema；字节级字段名以实现为准，语义不得分叉。  
> 目标：随引擎 M8、game_kit GK4、editor ED2–ED5 再冻结；本波仅保持跨层约定可读。

## 1. 所有权

| 数据 | 权威解释方 | 其它消费者 |
|---|---|---|
| 节点层级、Transform、相机、灯、网格/材质 **AssetId** | render_engine | editor 编辑；game_kit 只读或经 API 改 |
| 脚本组件类型名、脚本路径、公开字段 | game_kit | editor 显示/改字段；引擎加载时忽略未知块或原样保存 |
| Manifest / 依赖图 | render_engine cook | 全体 |
| 玩法存档 | game_kit | 与场景文件 **分离** |

原则：**一种文件格式可含多段；未知段须 round-trip 保留（编辑器/引擎不丢 game_kit 块）。**

## 2. 场景文件（逻辑结构）

```text
SceneDocument
  header: format_version, host_api_hint?
  nodes: Node[]
  references: AssetId[]          # 可选显式列表；也可从节点扫描
  extensions:
    game_kit?: { … }             # 可选
```

### Node（渲染向，引擎权威）

| 字段 | 说明 |
|---|---|
| `id` | 稳定 ID（字符串或 GUID） |
| `name` | 显示名 |
| `parent` | 父 id 或 null |
| `transform` | TRS（或矩阵）；约定局部空间 |
| `visible` | bool |
| `components` | 见下 |

### 组件（引擎已知）

| type | 载荷摘要 |
|---|---|
| `MeshRenderer` | mesh AssetId、material AssetId[] |
| `Camera` | 透视/正交参数 |
| `Light` | 方向/点/聚光参数 |
| `Sprite` / `Tilemap` | 2D（M16+） |
| … | 随里程碑扩展 |

### 组件（game_kit，引擎可透传）

| type | 载荷摘要 |
|---|---|
| `Script` | `script` 路径或模块名、`fields` 字典（仅公开字段） |
| `GameTag` | 可选标签 |

引擎 Runtime **可不执行** Script；加载后由 game_kit Module 绑定。

## 3. Prefab

```text
PrefabDocument
  header: format_version, prefab_id
  root: Node（可含子树）
  defaults: 可选默认覆盖表
```

- **实例化：** 深拷贝子树 → 新 Node id；记录 `prefab_id` + 可选 overrides。  
- **一期（GK4/ED5）：** 支持「放置实例 + 改 Transform」；嵌套 Prefab / 属性覆盖可后置。  
- Prefab 源文件进 Manifest；依赖闭包含其 mesh/材质/脚本资产。

## 4. 与 cook / 依赖图

1. 场景/Prefab 引用的每个 AssetId → Manifest 硬依赖边。  
2. Script 路径若作为资产 → 可选依赖边（开发期散文件也可）。  
3. 保存后 editor 可触发 C20 校验或 cook 刷新。

## 5. 版本与兼容

| 规则 | 说明 |
|---|---|
| `format_version` | 整数；读者须拒绝不支持的主版本 |
| 未知 `components[].type` | 保留字节/JSON 对象，不删除 |
| 未知字段 | 同左 |

## 6. 示例（示意 JSON）

```json
{
  "format_version": 1,
  "nodes": [
    {
      "id": "player",
      "name": "Player",
      "parent": null,
      "transform": { "t": [0, 0, 0], "r": [0, 0, 0, 1], "s": [1, 1, 1] },
      "visible": true,
      "components": [
        { "type": "MeshRenderer", "mesh": "meshes/hero", "materials": ["mats/hero"] },
        { "type": "Script", "script": "scripts/player_controller", "fields": { "speed": 5.0 } }
      ]
    }
  ]
}
```

## 7. 相关

- [HOST_API.md](HOST_API.md)  
- [HOSTING.md](HOSTING.md)  
- [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md) §6  
- [../../game_kit/docs/ARCHITECTURE.md](../../game_kit/docs/ARCHITECTURE.md)  
- [TOOLING.md](TOOLING.md) C20  
