# Learn 06 — RHI Triangle

## 目标

绕过 `Application`，直接使用 **`CreateDevice` + `DrawSimpleMesh`** 理解最小 RHI 帧循环：`BeginFrame → Clear → Draw → Present`。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_06_rhi_triangle
build\samples\learn\06_rhi_triangle\Debug\sample_06_rhi_triangle.exe
build\samples\learn\06_rhi_triangle\Debug\sample_06_rhi_triangle.exe --headless --headless_frames=2
```

## 代码地图

| 步骤 | API |
|---|---|
| 窗口 | `Window::Create` / `PumpEvents` |
| 设备 | `rhi::CreateDevice(Backend::D3D12, …)` |
| 着色器 | `sample_02_triangle_shaders` 输出的 `triangle.*.cso` |
| 绘制 | `SetupSimpleMesh` + `DrawSimpleMesh` |

## 必做练习

1. 对比 Sample 02：列出 `Application::Run` 帮你做了哪些事。
2. 在 headless 模式下观察返回的是 CPU stub 还是 D3D12（阅读 `backend.cpp`）。
3. 去掉 `Present` 看错误信息，理解交换链角色。

## 常见坑

- **Shader 依赖 02**：需先构建 `sample_02_triangle_shaders`。
- **Headless 无 HWND**：`CreateDevice` 走 `HeadlessDevice`，仍可调 `DrawSimpleMesh`。
- **忘记 PumpEvents**：窗口模式会假死；headless 可忽略。
