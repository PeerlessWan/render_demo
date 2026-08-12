# game_kit 约束

## 1. 依赖

1. 只依赖 `render_engine` **公开头**与文档约定的 Host API。  
2. 禁止依赖 `render_engine/engine/backends/**`、引擎内部匿名命名空间符号。  
3. 三方（Lua 等）仅出现在 `game_kit/script`；游戏业务代码不直链 VM 细节时可再包一层。  
4. 遵守引擎 [STANDARDS](../../render_engine/docs/STANDARDS.md) / [THIRD_PARTY](../../render_engine/docs/THIRD_PARTY.md) 精神：可替换、可诊断。

## 2. 线程与寿命

1. 脚本默认主线程；禁止在 IO 回调里直接改 Scene。  
2. 退出顺序：停玩法/VM → 取消异步 → 引擎 Shutdown。  
3. 不假设跨帧持有引擎临时对象（FG 瞬时、单帧 Event）。

## 3. 功能边界

1. 不实现渲染特性补丁「绕过引擎质量档」。  
2. 不实现完整导航网格/复制同步产品（可后置模块，须单独文档）。  
3. 玩法存档 ≠ 引擎场景文件；禁止混用一种格式冒充两种语义。  
4. **保持品类无关**：不把对话/背包/射击等系统做进本层；此类能力在 `genre_kits/*` 或 `games/<title>`（[LAYERS](../../docs/LAYERS.md)、ADR 0028）。  
5. 本层不得依赖 `genre_kits/*` 或 `games/*`（依赖只允许向下到引擎）。

## 4. 版本

1. 标明依赖的 `render_engine` [Host API](../../render_engine/docs/HOST_API.md) 版本（实现后）。  
2. 破坏性绑定变更走 game_kit 自身 semver，不偷偷依赖引擎私有布局。  
3. Prefab/场景格式遵循 [PREFAB_SCHEMA.md](../../render_engine/docs/PREFAB_SCHEMA.md)。  

## 5. 相关

- [SCRIPTING.md](SCRIPTING.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../render_engine/docs/HOSTING.md](../../render_engine/docs/HOSTING.md)  
