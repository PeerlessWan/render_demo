# Learn 01 — Clear（清屏）

> 跑通 `Application` 主循环，理解每帧 **BeginFrame → Clear →（可选绘制）→ Present**，并可选切换 Vulkan 后端。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_01_clear
build\samples\learn\01_clear\Debug\sample_01_clear.exe
# 可选：Vulkan 清屏
build\samples\learn\01_clear\Debug\sample_01_clear.exe --backend=vulkan
# 可选：Vulkan + lit cube（需旁路 SPIR-V）
build\samples\learn\01_clear\Debug\sample_01_clear.exe --backend=vulkan --mesh
```

Headless 冒烟（若支持）：

```powershell
build\samples\learn\01_clear\Debug\sample_01_clear.exe --headless --headless_frames=2
```

## 知识点

1. **应用壳与设备工厂**：`Application::Create` 根据 `ApplicationDesc.backend` 创建 Window + `IDevice`。
2. **帧循环契约**：每帧设备侧 `BeginFrame` → 业务回调 → `Present`；Clear 通常在 Begin 之后由 Application 或设备完成。
3. **清屏颜色**：`clear_color` 是 RGBA 浮点；本 demo 默认偏蓝灰，用于确认「画面不是未初始化垃圾」。
4. **后端切换**：`--backend=vulkan` 走 M17 路径；缺 `ENGINE_WITH_VULKAN` 时应失败可诊断，而不是假绿。
5. **可选 lit**：`--mesh` 仅在 Vulkan 下加载 `lit_cube_vk` SPIR-V，演示「清屏之上叠一层真绘制」。

## 名词解释

| 术语 | 含义 |
|---|---|
| **Swapchain** | 与窗口绑定的一组可 Present 的图像；清屏最终写入当前 backbuffer。 |
| **Clear** | 将 RT/深度写成常量值；是最简单的「有输出」验证。 |
| **IDevice** | 引擎 RHI 抽象；D3D12/Vulkan/Headless 实现同一接口。 |
| **Application** | 窗口、输入、设备、主循环的薄壳；业务画在 `Run` 回调里。 |
| **Backend** | 运行时图形 API 选择（本仓库主路径 D3D12，第二路径 Vulkan）。 |
| **SPIR-V** | Vulkan 着色器中间表示；由 DXC 从 HLSL 编译而来。 |

详见 [GLOSSARY.md](../../docs/learn/GLOSSARY.md)。

## 原理

```text
main
  → ApplicationDesc（标题、分辨率、clear_color、backend）
  → Application::Create（Window + CreateDevice）
  → Run(on_frame)
       每帧：BeginFrame → Clear(clear_color) → on_frame → Present
```

- **为什么先清屏？** 若跳过 Clear，上一帧残留或未定义内容会闪烁；教学上用固定颜色证明「命令真的提交了」。
- **Vulkan `--mesh`**：在回调里懒加载 `SetupLitMesh`，再 `SetFrameLighting` + `DrawLitCubes`；失败则打日志并停止重试（`lit_failed`），避免每帧刷错。
- **与产品 Sandbox 的关系**：Sandbox 同样走 Application，但叠加 RenderSystem、物理、UI；本课只保留壳。

## 代码地图

| 符号 / 文件 | 说明 |
|---|---|
| `samples/learn/01_clear/main.cpp` | 入口；解析 `--backend` / `--mesh` |
| `ApplicationDesc` | 窗口与清屏参数 |
| `Application::Create` / `Run` | 主循环 |
| `SetupLitMesh` / `DrawLitCubes` | Vulkan 可选 lit 路径 |
| `ExeDir()/shaders/*.spv` | 可执行文件旁 SPIR-V |

## 必做练习

1. 改 `clear_color` 为纯红 `(1,0,0,1)`，确认窗口立刻变红。
2. 对比默认 D3D12 与 `--backend=vulkan` 启动日志差异。
3. 在 Vulkan 下加 `--mesh`，确认立方体出现；故意删掉一个 `.spv`，确认错误可诊断。
4. （口头）画出 BeginFrame / Clear / Present 与 CPU 回调的时序。

## 常见坑

- **以为 Clear 是业务 API**：多数情况下由 Application 在回调前 Clear；业务里再 Clear 可能覆盖。
- **Vulkan 无 SDK / 未开 CMake 选项**：创建失败应看 `LogError`，不要改成静默回退 D3D12（除非你显式写回退策略）。
- **`--mesh` 却用 D3D12**：本 sample 的 SPIR-V 路径面向 Vulkan；D3D12 请走后续 triangle / lit 课。
- **Headless**：无窗口时仍验证循环跑通；像素观感用后续 golden / gpu-headless。
