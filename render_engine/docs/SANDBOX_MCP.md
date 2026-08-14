# Sandbox Harness + MCP

## 分工

| 路径 | 用途 |
|---|---|
| `sample_sandbox --gpu-headless` | CI 冒烟 / 读回断言 |
| `sample_sandbox --harness-stdio` | JSON 行协议控制（CI / 脚本） |
| `sandbox_mcp` | Cursor MCP 适配器（stdio JSON-RPC） |
| `ENGINE_GOLDEN_DUMP` + `tests/scripts/run_golden.py` | 像素准确回归 |

**准确度靠黄金图；彻底性靠 harness/MCP 扫开关与 Feature。**

## Harness 行协议

每行一个 JSON，响应一行 JSON：

```json
{"cmd":"ping"}
{"cmd":"query_features"}
{"cmd":"toggle","key":"taa"}
{"cmd":"set_quality","key":"medium"}
{"cmd":"camera","pos":[0,2,6]}
{"cmd":"frame","n":1}
{"cmd":"capture","path":"build/out.rgba"}
{"cmd":"quit"}
```

启动：

```bat
sample_sandbox.exe --harness-stdio --backend=d3d12
```

## Cursor 挂 MCP

在 MCP 配置中指向构建产物，例如：

```json
{
  "mcpServers": {
    "sandbox": {
      "command": "D:/workspace/media/render_demo/render_engine/build/tools/sandbox_mcp/Debug/sandbox_mcp.exe"
    }
  }
}
```

工具：`ping` / `query_features` / `capture` / `toggle`。GPU 截帧请配合 Sandbox harness 或 `run_golden.py`。
