# minecraft（生存核心环）

体素沙盒，玩法对标微软 Minecraft **早期生存循环**，不宣称全量对齐。方块为**纯色立方体**，不加载贴图。

依赖：`game_kit` + `render_engine` 公开 API。禁止直链 backends。

可执行文件：`minecraft_survival`（`render_demo/build_kits`）。

操作：主菜单新建/加载/创造；WASD 移动，RMB 视角，LMB 挖，RMB 放/开箱；I 背包，P 暂停，R 吃/重生，F 创造，F3 调试，F5 存档，`[` `]` 视距。暂停不用 Esc（引擎 Escape 关窗）。
