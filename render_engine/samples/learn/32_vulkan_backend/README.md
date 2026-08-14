# Learn 32 — Vulkan 后端

## 目标

在 `ENGINE_WITH_VULKAN=1` 时以 **Vulkan + headless** 创建 Application 并跑 2 帧；否则 **SKIP exit 0**。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_32_vulkan_backend
build\samples\learn\32_vulkan_backend\Debug\sample_32_vulkan_backend.exe --headless --headless_frames=2
```

## 代码地图

| 分支 | 行为 |
|---|---|
| `ENGINE_WITH_VULKAN=0` | 打印 SKIP，return 0 |
| `Backend::Vulkan` + headless | CreateDevice → HeadlessDevice（CI） |

## 必做练习

1. 对比 `--backend=d3d12` Sandbox 与 Vulkan sample。
2. 窗口模式跑 Sandbox `--backend=vulkan`（需 HWND）。
3. 阅读 `backend.cpp` Vulkan 分支。

## 常见坑

- **Headless 非真 VkInstance**：Vulkan+headless 走 HeadlessDevice stub。
- **Lit SPIR-V**：真 Vulkan 窗口路径才加载 lit cube。
