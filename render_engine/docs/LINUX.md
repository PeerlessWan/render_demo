# Linux + Vulkan（M18 · Mega-W11）

> 当前仓库 **以 Windows 为主路径**。本页给出 Linux（X11）上构建 Vulkan 的步骤与 CMake 开关；**运行时冒烟视自托管 CI / 实机**。Mega-W11 已接线：UNIX 跳过 D3D12、`Window::Create` → X11、`VK_KHR_xlib_surface`。

## 现状

| 项 | 状态 |
|---|---|
| Windows Vulkan（M17） | `backends/vulkan/vulkan_device.cpp`：Win32 surface + swapchain（`ENGINE_WITH_VULKAN`） |
| engine/platform/win32/ | 已实现 `Window::Create`（Win32 + headless）；**仅 WIN32 编译** |
| engine/platform/linux/ | **Mega-W11**：`window_x11`；`__linux__` + `ENGINE_HAS_X11` 时 `Window::Create` → X11；headless Ok；非 Linux 树为 Unavailable 桩 |
| D3D12 | Linux **不做**；CMake 跳过 `engine_d3d12` 与 D3D12 learn samples |
| X11 clear 路径 | 真窗可开；`TryCreateXlibSurface` + `CreateVulkanDevice` 接 `VK_KHR_xlib_surface`；独立 smoke 见下表 |
| **Wayland** | **W15 目标**：`window_wayland` + `ENGINE_HAS_WAYLAND`；`WAYLAND_DISPLAY` 探测；X11 仍为 present/CI 基线 |

旧占位页 [LINUX_VULKAN.md](LINUX_VULKAN.md) 已并入本页。

## Wayland（W15 / ADR 0039）

- `CreateWaylandWindowStub`：有 `libwayland-client` 且 `WAYLAND_DISPLAY` 时 `wl_display_connect`；否则 Unavailable。
- `Window::Create`：探测 Wayland 后仍映射 **X11** 窗口做 present（xdg-shell 全接线后续）；`ENGINE_FORCE_X11=1` 跳过 Wayland 探测。
- CI 基线仍为 **X11** + `xvfb-run`。
- 全树同步：将本仓库 `render_engine/` 同步到 Linux 主机后 `cmake -B build -DENGINE_LINUX_VK=ON && cmake --build build`。

## X11 clear 路径（Mega-W11）

目标链路（有 Linux 显示 + Vulkan 驱动时）：

1. `Window::Create` / `CreateX11WindowStub` → `__linux__` + `ENGINE_HAS_X11` 时 `XOpenDisplay` + `XCreateSimpleWindow`（无 `DISPLAY` 时 Unavailable；可用 `xvfb-run`）。
2. Vulkan `VK_KHR_xlib_surface`：`TryCreateXlibSurface` → `vkCreateXlibSurfaceKHR`（`DeviceDesc.native_window` = `X11Native*`）。
3. `sample_01_clear --backend=vulkan` 清屏一帧（需把引擎树同步到 Linux 主机后 cmake/ninja）。

本波验收：

- Windows 树仍可编译 `window_x11` TU（无 X11 头依赖）；非 `__linux__` 返回 Unavailable。
- Linux 主机 `find_package(X11)` 成功时定义 `ENGINE_HAS_X11`，非 headless 可开真窗；`Window::Create` 走 X11。
- UNIX 不建 `engine_d3d12`；`ENGINE_WITH_VULKAN` 在系统/SDK 头可用时默认 ON。
- `ENGINE_LINUX_VK` 在 Linux 上默认 ON；**不**静默安装 X11/Vulkan。

## 依赖（主机包，引擎不安装）

```bash
# Debian/Ubuntu 示例（自行安装，勿在引擎脚本里静默装包）
sudo apt install build-essential cmake ninja-build \
  libx11-dev libxi-dev libxkbcommon-dev \
  libvulkan-dev vulkan-tools   # 或 LunarG VULKAN_SDK
# 无图形会话的 SSH 冒烟可用：
#   sudo apt install xvfb
```

- **Vulkan SDK**：设置 `VULKAN_SDK`（或 `ENGINE_VULKAN_SDK`）指向含 `Include`/`include` + `vulkan/vulkan.h` 的前缀；也可用发行版 `/usr/include/vulkan` + `libvulkan`。
- **X11**：本波目标窗口后端；libX11 + 输入相关头。
- **Wayland**：明确 **后置**（见上节）。
- **OpenSSL（可选 HTTPS）**：见 [THIRD_PARTY.md](THIRD_PARTY.md)；`ENGINE_WITH_OPENSSL`；**引擎不安装 OpenSSL**。

## CMake 标志

```bash
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENGINE_WITH_VULKAN=ON \
  -DENGINE_LINUX_VK=ON \
  -DENGINE_BUILD_SAMPLES=ON \
  -DENGINE_BUILD_LEARN_SAMPLES=OFF \
  -DENGINE_WITH_OPENSSL=OFF
# 系统 Vulkan 头时可不设 VULKAN_SDK；有 SDK 时：
#   -DENGINE_VULKAN_SDK="$VULKAN_SDK"
# 若本机已有 OpenSSL 且要开 HTTPS：
#   -DENGINE_WITH_OPENSSL=ON  （可加 -DOPENSSL_ROOT_DIR=...）
# 切勿在配置脚本里 apt/brew 安装 OpenSSL / MsQuic
```

| 选项 | 含义 |
|---|---|
| `ENGINE_WITH_VULKAN` | 编真 Vulkan `IDevice`（需 SDK 或系统 Vulkan 开发包）；Linux 有头时默认 ON |
| `ENGINE_LINUX_VK` | Linux Vulkan + X11；Linux 主机默认 ON |
| `ENGINE_WITH_OPENSSL` | 系统 OpenSSL 经 `find_package` 成功时启用 HTTPS；失败则 HTTPS Unavailable |

Linux 上 CMake **跳过** `engine_d3d12` 与 D3D12 learn / Sandbox；保留 `sample_01_clear`（链 `engine_app` + Vulkan）。`engine_platform` 链 X11；`Window::Create` 在 `__linux__` 实现于 `window_x11.cpp`。

## 建议冒烟（有 Linux CI / 实机时）

```bash
cmake --build build-linux -j
ctest --test-dir build-linux -R unit --output-on-failure
# 有显示与驱动时：
# ./build-linux/samples/.../sample_01_clear --backend=vulkan
# 无 DISPLAY 的 SSH 会话：
# xvfb-run -a ./build-linux/samples/.../sample_01_clear --backend=vulkan

# 最小 X11+VK surface（不依赖完整引擎树）：
# g++ -std=c++20 tools/linux_smoke/x11_vk_smoke.cpp -o /tmp/x11_vk_smoke -lX11 -lvulkan
# xvfb-run -a /tmp/x11_vk_smoke
```

无 GPU / 无 X11 显示：单测与 headless 可先跑；交互清屏需 `DISPLAY` 或 `xvfb-run`。

## 实机冒烟记录

| 日期 | 主机 | 用户 | 结果 | 说明 |
|---|---|---|---|---|
| 2026-08-18 | 172.30.2.200 | breton | **OK（部分）** | SSH 连通。uname：Ubuntu 24.04 系（7.0.0-28-generic x86_64）。已有 cmake 3.28、ninja、g++ 13、libx11-dev、libvulkan-dev、vulkan-tools、pkg-config x11/vulkan。`VULKAN_SDK` 未设置（系统头/库可用）。`DISPLAY` 在 SSH 为空。GPU：NVIDIA RTX A4000。**最小 X11+VK smoke**（`tools/linux_smoke/x11_vk_smoke.cpp`）：编译 OK；裸跑 `XOpenDisplay` 失败；`xvfb-run` 下 `vkCreateXlibSurfaceKHR` **OK**。引擎全树未同步到该机，完整 `cmake/ninja` 未跑。Wayland：本波后置，未测。 |
| 2026-08-18（早） | 172.30.2.200 | breton | OK（部分） | 初探：X11 开窗（xvfb）OK；当时引擎尚未跳过 D3D12 / 接 surface。 |

缺口摘要（相对完整引擎 Linux Vulkan clear）：

1. 需将 `render_engine` 同步到 Linux 主机后再验 `cmake -G Ninja` + `sample_01_clear --backend=vulkan`。
2. 交互显示需本机 `DISPLAY` 或 `xvfb-run`；`VULKAN_SDK` 可选（系统包足够时）。

## 已知缺口

1. **完整引擎树实机 cmake** — 代码已门控；主机尚无同步本仓库树。
2. **Wayland** — **明确后置**；本波要求 X11。
3. **Vulkan Video** — 与 D3D12VA 对等验收后置；能力不足 SKIP。
4. **Sandbox / learn samples** — Linux 上跳过（D3D12/DXC）；仅保留 `01_clear` 作 Vulkan 入口。

## 相关

- [PLAN.md](PLAN.md) M18 · [VULKAN_PARITY.md](VULKAN_PARITY.md) · [KNOWN_GAPS.md](KNOWN_GAPS.md) G19
- ADR [0020](learn/adr/0020-windows-d3d12-vulkan-linux-vulkan.md) · [ADR 0038](learn/adr/0038-mega-w11-parity.md) · QUIC [ADR 0031](learn/adr/0031-m19-quic-skip-msquic.md)
