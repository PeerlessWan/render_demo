# Sandbox Harness + MCP

> **计划口径（[PLAN.md](PLAN.md) §3.21 / §3.1）**：**Harness 保留、不再加命令**；**MCP 冻结不扩**，CI 不依赖。测试加深准优先于广，不靠扩 MCP。

## 分工

| 路径 | 用途 |
|---|---|
| `sample_sandbox --gpu-headless` | CI 冒烟 / 读回断言 |
| `sample_sandbox --harness-stdio` | JSON 行协议控制（CI / 脚本） |
| `sandbox_mcp` | Cursor MCP 适配器：拉起真机 Sandbox 并转发 tools/call |
| `ENGINE_GOLDEN_DUMP` + `tests/scripts/run_golden.py` | 像素准确回归 |
| `tests/scripts/run_matrix_smoke.py` | 质量档 × toggle × backend 冒烟 |

**准确度靠黄金图；彻底性靠 harness 扫开关（`run_matrix_smoke.py`）。MCP 不是门禁。**

## Harness 行协议

每行一个 JSON，响应一行 JSON：

```json
{"cmd":"ping"}
{"cmd":"query_features"}
{"cmd":"toggle","key":"taa"}
{"cmd":"set_quality","key":"medium"}
{"cmd":"camera","pos":[0,2,6]}
{"cmd":"frame","n":1}
{"cmd":"profiler_snapshot"}
{"cmd":"capture","path":"build/out.rgba"}
{"cmd":"quit"}
```

`toggle` / `set_quality` / `profiler_snapshot` / `frame` 会改真实 `fx` / quality / 读 profiler（非空 accepted）。

启动：

```bat
sample_sandbox.exe --harness-stdio --backend=d3d12
```

## Cursor 挂 MCP

```json
{
  "mcpServers": {
    "sandbox": {
      "command": "D:/workspace/media/render_demo/render_engine/build/tools/sandbox_mcp/Debug/sandbox_mcp.exe",
      "env": {
        "ENGINE_SANDBOX_EXE": "D:/workspace/media/render_demo/render_engine/build/samples/Sandbox/Debug/sample_sandbox.exe"
      }
    }
  }
}
```

工具：`ping` / `query_features` / `toggle` / `set_quality` / `frame` / `profiler_snapshot` / `capture`。  
MCP 启动时 spawn `sample_sandbox --harness-stdio`，tools/call 转发 JSON 行；`capture` 返回路径。

Agent 一轮：`set_quality` → `toggle` → `capture` → `run_golden.py` / `compare_golden.py`。
