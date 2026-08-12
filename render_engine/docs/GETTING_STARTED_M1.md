# M1 可执行清单（Getting Started）

> 对应 [PLAN.md](PLAN.md) §7 / 里程碑 **M1**。  
> 目标：Windows 上 **D3D12 清屏窗口** + **Catch2 单测可跑**；代码与文档目录对齐 [ARCHITECTURE.md](ARCHITECTURE.md) §3。

## 0. 前置（本机）

| 项 | 要求 |
|---|---|
| OS | Windows 10/11 x64 |
| 编译器 | MSVC（建议 VS 2022）+ Windows SDK（含 D3D12） |
| CMake | **≥ 3.24** |
| Git | 用于拉取仓库；Catch2 推荐 CMake `FetchContent` |

**本阶段不做：** Vulkan 设备、shader_compile 强制落地（清屏可不编译 HLSL；着色器工具链验收放 **M2**，见 [TOOLING.md](TOOLING.md)）。

## 1. 目录骨架（一次性创建）

在 **`render_engine/`** 下创建（空目录可放 `.gitkeep`）：

```text
CMakeLists.txt                 # render_engine 根
cmake/                         # 可选：EngineOptions.cmake
engine/
  CMakeLists.txt
  core/          # Result, Log, Math, Clock, Config …
  platform/      # win32 窗口
  input/         # DeviceHub / ActionMap 骨架（可先空接口）
  render/rhi/    # IRHIDevice 等
  backends/d3d12/
tests/
  CMakeLists.txt
  unit/          # Catch2
third_party/     # 可空；Catch2 用 FetchContent 则可不落盘
samples/
  learn/01_clear/   # 可选：与清屏同一可执行或薄封装
shaders/         # 可空（M1）
assets/          # 可空（M1）
```

`docs/` 已在 `render_engine/docs/`；工作区根 `tools/README.md` 过渡期保留，目标迁入 `render_engine/tools/`。

## 2. CMake 约定（最小）

根 `CMakeLists.txt` 要点：

- `project(render_engine LANGUAGES CXX)`，`CXX_STANDARD 20`（与 [STANDARDS.md](STANDARDS.md) 一致）  
- option：`ENGINE_BUILD_TESTS`（默认 ON）、`ENGINE_BUILD_SAMPLES`（默认 ON）  
- `add_subdirectory(engine)`；可选 `samples`、`tests`  
- Catch2：`FetchContent_Declare(Catch2 …)` + `Catch2::Catch2WithMain`  
- 目标示例：`engine_core`（静态库）、`engine_d3d12`、可执行 `sample_01_clear` 或 `sandbox_clear`  
- 链接：`d3d12.lib` `dxgi.lib`（M1）

验收命令（落地后）：

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug -R unit --output-on-failure
```

手动：运行清屏 Sample，窗口客户区为纯色（或清屏色），关闭不崩溃。

## 3. 实现顺序（与 PLAN §7 对齐）

1. **CMake + `engine/core`**：`Result` / 日志 / 基础 math（可单测）  
2. **`platform/win32`**：创建窗口、消息泵、客户区尺寸  
3. **`render/rhi` + `backends/d3d12`**：Device / Queue / Swapchain / 清屏一帧  
4. **`application` 骨架**（可极简）：主循环 `Pump` → `BeginFrame` → Clear → `Present`  
5. **`tests/unit`**：至少 2 个用例（如 `Vec3` 长度、`Config` 读写）+ CTest  
6. **更新根 README**「编译运行」为真实命令  

可选同步：`input` 空接口 + ADR 0011 目录占位（不挡清屏）。

## 4. M1 验收（完成定义）

| # | 条件 |
|---|---|
| 1 | `cmake` 配置与 Debug 构建成功 |
| 2 | 清屏窗口可见，可关窗退出 |
| 3 | `ctest -R unit` 全绿 |
| 4 | 业务/Sample **不** `#include <d3d12.h>`（仅 `backends/d3d12`） |
| 5 | 根 README 编译说明与实际命令一致 |

**非 M1：** Vulkan、IBL、纹理三角形、shader_compile CI、黄金图。

## 5. 文档回写（做完勾选）

- [ ] [PLAN.md](PLAN.md) §6：M1 标为进行中/完成  
- [ ] [learn/PATH.md](learn/PATH.md) CH00/CH01 可指向真实路径  
- [ ] 若 Catch2 引入方式定稿 → 填 [THIRD_PARTY.md](THIRD_PARTY.md) 许可表一行  

## 6. 相关

- [PLAN.md](PLAN.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [STANDARDS.md](STANDARDS.md)  
- [TESTING.md](TESTING.md)  
- [TOOLING.md](TOOLING.md)（M2 起强制 shader_compile）  
- [learn/adr/0020-windows-d3d12-vulkan-linux-vulkan.md](learn/adr/0020-windows-d3d12-vulkan-linux-vulkan.md)  
