# editor 定位

## 是

- 面向关卡/Prefab **摆放与属性编辑** 的独立工程  
- 使用 `render_engine` 渲染视口与 Debug 绘制  
- 保存结果为引擎可加载的场景/清单格式（与 cook 依赖图一致）  
- 可选：显示 `game_kit` 脚本组件字段、跳转脚本文件  

## 不是

- 不是引擎内置模块（不链进 `engine` 热路径）  
- 不是 UE/Unity 级完整编辑器（无材质节点图、无 UMG 可视化、无动画状态机编辑器一期）  
- 不替代 Blender/DCC；网格/贴图仍建议外部制作 + cook  
- 不是游戏 Runtime（Play 模式可有，但是编辑器功能）  

## 与默认工具链关系

```text
DCC ──► tools/cook（必有路径）
              ▲
编辑器 ──保存──┘  同一 Manifest / 场景格式
```

无编辑器时，仅 DCC+CLI 仍可完成引擎验收。

## 一句话

> **editor = 视口 + 检视/存盘；render_engine = 画得出来；game_kit = 通用玩法壳；genre_kits / games = 品类与内容。**

## 相关

- [CONSTRAINTS.md](CONSTRAINTS.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../render_engine/docs/learn/adr/0025-toolchain-minimum-viable.md](../../render_engine/docs/learn/adr/0025-toolchain-minimum-viable.md)  
- [../../render_engine/docs/learn/adr/0027-hosting-script-editor-boundary.md](../../render_engine/docs/learn/adr/0027-hosting-script-editor-boundary.md)  
- [../../render_engine/docs/learn/adr/0028-genre-kits-layering.md](../../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
