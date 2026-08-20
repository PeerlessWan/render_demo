# game_kit Lua DAP（VS Code 骨架）

不发布市场。把本目录作为未打包扩展打开，或在 `.vscode/launch.json` 里指向它。

## 用法

1. 运行时 `DapSession::Listen(4711)`（hook 内阻塞直到 `continue`）。
2. VS Code 安装/打开本扩展后 F5，attach `127.0.0.1:4711`。
3. Adapter 把 VS Code 的 DAP `Content-Length` 帧原样转到 TCP。

协议子集：`initialize` / `setBreakpoints` / `continue` / `next` / `stackTrace` / `scopes` / `variables` / `threads`。
