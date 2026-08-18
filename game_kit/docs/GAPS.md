# game_kit 缺口

| ID | 缺口 | 说明 | 对应 |
|---|---|---|---|
| GK-G01 | 无代码 | **已写 GK0–GK5 接线** | PLAN GK0 |
| GK-G02 | Host API 未实现冻结 | **已冻 v0.1 子集**（`kHostApiVersion` + Overlap/Contacts） | HOSTING |
| GK-G03 | 语言未最终锁定 | **已用 Lua 5.4**（FetchContent） | SCRIPTING |
| GK-G04 | Prefab schema 未实现冻结 | **v3 Frozen**；Capture/Apply 写 Script+GameTag；editor StampMeta 走组件 | GK4 / editor |
| GK-G05 | 无热重载实现 | **已写**：`ScriptHotReload` + `Reload(preserve_state)` 可保留 `persist` 表 | SCRIPTING |
| GK-G06 | 无玩法存档格式 | **槽 v1 JSON + WorldSnapshot v1**（rot/scale/tags/script/persist） | GK1 |
| GK-G07 | 无导航/同步（产品级） | **已写**：Physics/World 烘焙 + `moveAlongSurface` 贴路 + DetourCrowd；`ReplicationSession` 含旋转 slerp。无独立服 | POSITIONING |
| GK-G08 | 绑定生成器 | **已写** `api.json` → `lua_api_reg.inc`（C 实现仍手写） | GK5 |
| GK-G09 | 无品类 kit | 多类型复用时按 [LAYERS](../../docs/LAYERS.md) 建 `genre_kits/*`；不阻塞本层 | ADR 0028 |
| GK-G10 | 骨骼动画绑定 | **已写** `AnimPlayer` 包引擎 `AnimationStateMachine`；Notify / 根运动 | SCRIPTING |
| GK-G11 | 脚本调试器 / Luau | **DAP 子集**（initialize/breakpoints/stackTrace/variables + hook 内阻塞暂停）；无 VS Code 扩展、不换 Luau | SCRIPTING |

## 相关

- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [PLAN.md](PLAN.md)  
- [../../render_engine/docs/KNOWN_GAPS.md](../../render_engine/docs/KNOWN_GAPS.md) C19  
- [../../render_engine/docs/learn/adr/0028-genre-kits-layering.md](../../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
