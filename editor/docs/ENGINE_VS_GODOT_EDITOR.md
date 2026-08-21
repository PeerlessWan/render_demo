# 编辑器 vs Godot（中小关卡尺子）

> **W25 / ADR 0002 + ED-G 真实现波**：自评 **≈95%**（仅限下列尺子）。  
> **不是** GDScript / 材质节点图 / UMG / 一站式 FBX / 导出与插件商店（见 [GAPS.md](GAPS.md) §4）。

## 尺子

对标 **Godot 4 中小 PC 关卡/Prefab 编辑器产品观感**：视口、场景树、检视器、内容浏览器、Play-in-Editor、Prefab、地形/Tile/Anim/Bake 等 **已交付能力** 的 UX。

## 自评

| 能力 | vs Godot 中小关卡 | 说明 |
|---|---|---|
| 视口飞控 / 多视口 / 2D | ≈95% | Persp + Top/Front/Side 正交 + 四分屏；2D ortho |
| 场景树 / 搜索 / 多选 | ≈100% | 层级 dock |
| 检视器 TRS / 可见 / 脚本字段 | ≈95% | 无材质节点图；网格 Combo 含 Manifest |
| 内容浏览器拖放 | ≈95% | 递归扫 `editor/content`；松手射线落点 |
| Play / 暂停 / 退出恢复 | ≈100% | Ctrl+P；物理建一次；Space/Pause |
| Prefab 放置 / override | ≈95% | 旋转+fields；Apply/Revert 子树 |
| 地形雕刻 / Tile / Anim | ≈95% | 视口笔刷；Undo 含 MCP 路径 Upload |
| Bake / Lint / 热重载 | ≈90% | Bake 闭环；热重载 albedo 仅设备 0/1 两槽 |
| 快捷键 / 输出面板 / 保存反馈 | ≈100% | W25 封板 |
| GDScript / 着色器图 / 导出 | **不对齐** | 刻意不做 |

**总评（该尺子）：≈95%。** 勿与「全能编辑器 / UE5」混谈。

## 已知天花板（不算 §4，但是 cap）

- 热重载：`UploadLitAlbedoRgba` 仅 slot 0/1；gltf 网格热重传未做满。
- Anim：状态机+曲线驱动选中 Y，不绑骨骼蒙皮编辑器。

## 相关

- [PLAN.md](PLAN.md) · [FEATURES.md](FEATURES.md) · [adr/0002-w25-godot-editor-feel-100.md](adr/0002-w25-godot-editor-feel-100.md)
