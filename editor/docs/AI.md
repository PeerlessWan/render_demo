# editor MCP（给 Agent）

人用 `editor_app` 点视口；**Agent 走 `editor_mcp`**。若 `editor_app` 已在跑，`editor_mcp` 会连 `127.0.0.1:17864` 把 JSON-RPC 转到**当前窗口那份场景**；否则用进程内空 `EditorHost`。

与 GUI 共用 [`cmd/session.h`](../cmd/session.h) 的 `ApplyOp`。

## 接线

1. 编译：`cmake --build render_demo/build_kits --config Release --target editor_mcp`
2. 复制 [`mcp/cursor.mcp.json.example`](../mcp/cursor.mcp.json.example) 到工作区 `.cursor/mcp.json`（按本机 `editor_mcp.exe` 路径改 `command`）
3. 工作目录建议 `render_demo/`，以便扫到 `editor/content`

## 工具怎么用

| 场景 | 工具顺序 |
|---|---|
| 看当前关 | `editor_dump` |
| 打开示例 | `editor_open` path=`editor/content/start.json` |
| 摆一个方块 | `editor_create` kind=`cube` → `editor_set_transform` |
| 改脚本 | `editor_select` name=… → `editor_set_script` |
| 改名 | `editor_select` → `editor_set_name`（GUI 也可 InputText） |
| 重父级 | `editor_set_parent` parent=名或空 |
| 试玩逻辑 | `editor_play` → `editor_step` / `editor_pause` → `editor_stop` |
| 热重载 / 烘焙 / 校验 | `editor_hot_reload` / `editor_bake` / `editor_lint` |
| 截图 | 与 `editor_app` 同进程时 `editor_screenshot` 回读当前帧 PPM；无 GPU 的独立 `editor_mcp` 返回 `isError` |

Play 中禁止 create / destroy / duplicate / open / save / set_name / set_mesh / set_script / set_parent。出错时 `isError: true`。

独立 `editor_mcp` 无窗口时无 GPU，截图失败；连上正在跑的 `editor_app` 则走同一 Device 回读。状态以 `editor_dump` JSON 为准。
