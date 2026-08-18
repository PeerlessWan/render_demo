# game_kit 缺口

| ID | 缺口 | 说明 | 对应 |
|---|---|---|---|
| GK-G01 | 无代码 | **已写 GK0–GK5 接线** | PLAN GK0 |
| GK-G02 | Host API 未实现冻结 | 草案：[HOST_API.md](../../render_engine/docs/HOST_API.md)；随 M4–M9 | HOSTING |
| GK-G03 | 语言未最终锁定 | **已用 Lua 5.4**（FetchContent） | SCRIPTING |
| GK-G04 | Prefab schema 未实现冻结 | **GK4 已按草案落地**（不改引擎序列化） | GK4 / editor |
| GK-G05 | 无热重载实现 | **已写**：`ScriptHotReload` + `ScriptComponent` Reload（不毁 Device） | SCRIPTING |
| GK-G06 | 无玩法存档格式 | **槽 v0 JSON 已写** | GK1 |
| GK-G07 | 无导航/同步 | 刻意后置 | POSITIONING |
| GK-G08 | 绑定生成器 | 可手写 MVP，后置工具；本波白名单已手写 | GK5 |
| GK-G09 | 无品类 kit | 多类型复用时按 [LAYERS](../../docs/LAYERS.md) 建 `genre_kits/*`；不阻塞本层 | ADR 0028 |
| GK-G10 | 骨骼动画绑定 | `play_anim` 无宿主时返回 false；World 无 Anim 组件，不接骨骼系统 | SCRIPTING |

## 相关

- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [PLAN.md](PLAN.md)  
- [../../render_engine/docs/KNOWN_GAPS.md](../../render_engine/docs/KNOWN_GAPS.md) C19  
- [../../render_engine/docs/learn/adr/0028-genre-kits-layering.md](../../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
