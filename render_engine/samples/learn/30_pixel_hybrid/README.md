# Learn 30 — 2D/像素混合（Sprite + 3D + Tiled）（选修）

> 在 **3D lit cube** 帧上叠加 **nearest 采样 Sprite 列表**，并加载 **tiny Tiled JSON**（多图层 / tileset 图路径 / 碰撞层 gid），理解 M16 混合管线与像素整数缩放。

**对齐里程碑**：M16（Tilemap 导入）+ M20（`IntegerScale`）。增量见 CH34 / [MIXED_PICK.md](../../../docs/MIXED_PICK.md)。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_30_pixel_hybrid
build\samples\learn\30_pixel_hybrid\Debug\sample_30_pixel_hybrid.exe --headless --headless_frames=2
```

窗口：左上角三枚 32×32 精灵（`pixel` atlas 帧 0/1/2）。  
日志：`Sorted sprites count=3`；`Tiled layers=…`；`Collision grid …`；`IntegerScale …`。

CMake target：**`sample_30_pixel_hybrid`**。内容：`content/tiny_map.json`。

## 知识点

1. **Sprite 字段**：atlas_id、frame、position、size、sort_layer、sort_y、nearest、color。
2. **SortSprites**：先 sort_layer 后 sort_y；决定叠放顺序。
3. **DrawFrame 重载**：第五参数 `&sprites` 进入 FG sprite batch。
4. **nearest=true**：点采样像素风；产品配合整数缩放。
5. **3D+2D Hybrid**：RenderScene cube + sprite 列表同一 Present。
6. **atlas_id="pixel"**：依赖资产系统识别；缺则 Fail 可诊断。
7. **sort_layer=1**：三 sprite 同层，靠 sort_y 区分前后。
8. **alpha 混合**：sprite color.a=0.9；与 3D 合成顺序在 sprite pass。
9. **LoadTiledJson**：多图层；`tileset_image` 绑定；`collision` 层可 `ExportCollisionGids`。
10. **IntegerScale**：窗口相对设计分辨率的整数倍率（见 M20）。

## 名词解释

| 术语 | 含义 |
|---|---|
| **Sprite** | 2D 精灵实例。 |
| **SpriteAtlas** | 图集与帧矩形。 |
| **SortLayer** | 整数绘制层。 |
| **Y-sort** | sort_y 模拟深度。 |
| **Nearest** | 最近邻过滤。 |
| **Pixel pipeline** | Nearest + 整数缩放 + 对齐。 |
| **Hybrid** | 3D 与 2D 同帧。 |
| **Tilemap** | 瓦片地图（Tiled JSON 导入）。 |
| **Collision gid grid** | 碰撞层导给物理的只读 gid 表。 |
| **IntegerScale** | 多 DPI 下取整缩放因子。 |

详见 [GLOSSARY.md](../../docs/learn/GLOSSARY.md) 中 Sprite、Tilemap、Y-sort、Pixel pipeline。

## 原理

### Sprite 构造

```text
3 sprites:
  atlas_id="pixel", frame=0,1,2
  position (40+i*48, 40), size 32x32
  sort_layer=1, sort_y=i, nearest=true
SortSprites(sprites)
```

### Tiled

```text
LoadTiledJson(content/tiny_map.json)
  → ground / decor / collision 三层
  → tileset_image = "tiles.png"
ExportCollisionGids → 4x4 gid 网格
```

### 每帧

```text
DrawFrame(device, render_scene, env, aspect, &sprites)
  → Lit 3D cube
  → Post（Low 可能简化）
  → Sprite batch（nearest sample atlas）
  → Present
```

```mermaid
flowchart TB
  RS[RenderScene 3D] --> DF[DrawFrame]
  SP[Sprites sorted] --> DF
  TM[Tiled layers] --> LOG[Log / collision export]
  DF --> L[Lit]
  L --> P[Post]
  P --> S2[Sprite batch]
  S2 --> PR[Present]
```

## 代码地图

| 符号 / 文件 | 说明 |
|---|---|
| `samples/learn/30_pixel_hybrid/main.cpp` | sprite + Tiled + IntegerScale + DrawFrame |
| `content/tiny_map.json` | 迷你多图层地图 |
| `engine/render2d/sprite.h` | `Sprite`、`LoadTiledJson`、`ExportCollisionGids` |
| `engine/mixed/pick.h` | `IntegerScale` / Pick |
| `engine/render/render_system.h` | sprites 参数 |

## 必做练习

1. 改第三 sprite `sort_y` 最小，确认绘制顺序。
2. `nearest=false` 对比边缘（放大 atlas 时更明显）。
3. 增 layer 0 与 2 各一 sprite，验证层优先。
4. 读 `SortSprites` 实现，相等键是否稳定排序。
5. 改 `tiny_map.json` 碰撞层名字，确认 `ExportCollisionGids` 仍识别（含 collision 子串）。
6. 故意错 `atlas_id`，读 DrawFrame Status。
7. 改窗口/设计分辨率，观察 `IntegerScale` 日志。
8. （对比 CH29）Sprite pass vs ImGui pass 输入差异。

## 常见坑

- **看不见 sprite**：atlas 未加载、position 出屏、post 盖住。
- **Headless 不验像素**：count / Tiled / IntegerScale 日志即可。
- **碰撞层未导**：检查层名是否含 `collision`。
- **与 UI 混淆**：CH29 走 ImGui/quads；本章 render2d。
- **3D 深度忽略**：简单混合可能不写深度；CH34 谈 2D 深度。
- **sort_y 方向约定**：以实现为准；改值前读 SortSprites。

## 与 CH34 的增量（预告）

| 主题 | CH30 本 demo | CH34 增量 |
|---|---|---|
| Sprite 排序 | SortSprites | 深度/拣选策略 |
| Tilemap | LoadTiledJson 已接入 | 流式 / 更大图 |
| 整数缩放 | IntegerScale 日志 | viewport 裁剪 Present |
| Motion Vectors | SKIP | 2D MV 与 TAA 交互 |

完成本章后应能口头回答：**为何 pixel 游戏要 Nearest + 整数缩放？** 以及 **Y-sort 在侧视平台关卡何时不够？**
