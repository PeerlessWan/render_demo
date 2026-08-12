# game_kit 缺口

| ID | 缺口 | 说明 | 对应 |
|---|---|---|---|
| GK-G01 | 无代码 | 仅文档 | PLAN GK0 |
| GK-G02 | Host API 未实现冻结 | 草案：[HOST_API.md](../../render_engine/docs/HOST_API.md)；随 M4–M9 | HOSTING |
| GK-G03 | 语言未最终锁定 | 文档默认 Lua，实现前可 ADR | SCRIPTING |
| GK-G04 | Prefab schema 未实现冻结 | 草案：[PREFAB_SCHEMA.md](../../render_engine/docs/PREFAB_SCHEMA.md) | GK4 / editor |
| GK-G05 | 无热重载实现 | GK3 | SCRIPTING |
| GK-G06 | 无玩法存档格式 | GK1 | FEATURES |
| GK-G07 | 无导航/同步 | 刻意后置 | POSITIONING |
| GK-G08 | 绑定生成器 | 可手写 MVP，后置工具 | GK5 |
| GK-G09 | 无品类 kit | 多类型复用时按 [LAYERS](../../docs/LAYERS.md) 建 `genre_kits/*`；不阻塞本层 | ADR 0028 |

## 相关

- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [PLAN.md](PLAN.md)  
- [../../render_engine/docs/KNOWN_GAPS.md](../../render_engine/docs/KNOWN_GAPS.md) C19  
- [../../render_engine/docs/learn/adr/0028-genre-kits-layering.md](../../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
