# Linux + Vulkan（M18 · Mega-W10）

> 当前仓库 **以 Windows 为主路径**。本页给出 Linux（X11）上构建 Vulkan 的步骤与 CMake 开关；**运行时冒烟视自托管 CI / 实机**，本波不承诺本机 Windows 树能交叉编出完整 Linux 二进制。

## 现状

| 项 | 状态 |
|---|---|
| Windows Vulkan（M17） | ackends/vulkan/vulkan_device.cpp：Win32 surface + swapchain（ENGINE_WITH_VULKAN） |
| engine/platform/win32/ | 已实现 Window::Create（Win32 + headless） |
| engine/platform/linux/ | **Mega-W10**：window_x11.h + window_x11.cpp；__linux__ + ENGINE_HAS_X11 时 XOpenDisplay / XCreateSimpleWindow；headless 仍 Ok；非 Linux 树为 Unavailable 桩 |
| D3D12 | Linux **不做**；CMake 应只开 Vulkan |
| X11 clear 路径 | 真窗可开；TryX11ClearPathStub（VK_KHR_xlib_surface 未接）→ SKIP |
| **Wayland** | **本波明确后置**；本波要求 **X11**（见下方） |

旧占位页 [LINUX_VULKAN.md](LINUX_VULKAN.md) 已并入本页。

## Wayland（明确后置）

- **本波（Mega-W10）不做 Wayland** 窗口 / VK_KHR_wayland_surface 接线。
- 显示与清屏冒烟以 **X11**（libX11 + VK_KHR_xlib_surface 后续）为唯一目标后端。
- Wayland 留待后续波次；文档与 ENGINE_LINUX_VK 文案均按「X11 required this wave」理解。

## X11 clear 路径（Mega-W10）

目标链路（有 Linux 显示 + Vulkan 驱动时）：

1. CreateX11WindowStub → __linux__ + ENGINE_HAS_X11 时 XOpenDisplay + XCreateSimpleWindow（无 DISPLAY 时 Unavailable；可用 xvfb-run）。
2. Vulkan VK_KHR_xlib_surface / xcb 创建 surface（**尚未接线**）。
3. sample_01_clear --backend=vulkan 清屏一帧。

本波验收：

- Windows 树仍可编译 window_x11 TU（无 X11 头依赖）；非 __linux__ 返回 Unavailable。
- Linux 主机 ind_package(X11) 成功时定义 ENGINE_HAS_X11，非 headless 可开真窗。
- TryX11ClearPathStub 诚实 Unavailable SKIP，直至 surface 接线。
- ENGINE_LINUX_VK=ON 仅声明意图并打印缺口；**不**静默安装 X11/Vulkan。

## 依赖（主机包，引擎不安装）

`ash
# Debian/Ubuntu 示例（自行安装，勿在引擎脚本里静默装包）
sudo apt install build-essential cmake ninja-build \
  libx11-dev libxi-dev libxkbcommon-dev \
  vulkan-sdk   # 或发行版 vulkan-headers / libvulkan-dev + LunarG SDK
# 无图形会话的 SSH 冒烟可用：
#   sudo apt install xvfb
`

- **Vulkan SDK**：设置 VULKAN_SDK（或 ENGINE_VULKAN_SDK）指向含 Include/vulkan/vulkan.h 的前缀；也可用发行版 /usr/include/vulkan + libvulkan。
- **X11**：本波目标窗口后端；libX11 + 输入相关头。
- **Wayland**：明确 **后置**（见上节）。
- **OpenSSL（可选 HTTPS）**：见 [THIRD_PARTY.md](THIRD_PARTY.md)；ENGINE_WITH_OPENSSL；**引擎不安装 OpenSSL**。

## CMake 标志

`ash
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENGINE_WITH_VULKAN=ON \
  -DENGINE_VULKAN_SDK="$VULKAN_SDK" \
  -DENGINE_LINUX_VK=ON \
  -DENGINE_BUILD_SAMPLES=ON \
  -DENGINE_BUILD_LEARN_SAMPLES=OFF \
  -DENGINE_WITH_OPENSSL=OFF
# 若本机已有 OpenSSL 且要开 HTTPS：
#   -DENGINE_WITH_OPENSSL=ON  （可加 -DOPENSSL_ROOT_DIR=...）
# 切勿在配置脚本里 apt/brew 安装 OpenSSL / MsQuic
`

| 选项 | 含义 |
|---|---|
| ENGINE_WITH_VULKAN | 编真 Vulkan IDevice（需 SDK 或系统 Vulkan 开发包） |
| ENGINE_LINUX_VK | **声明意图**：Linux Vulkan + X11 clear；ON 时打印状态与缺口 |
| ENGINE_WITH_OPENSSL | 系统 OpenSSL 经 ind_package 成功时启用 HTTPS；失败则 HTTPS Unavailable |

Windows 树当前仍链 platform/win32 与 engine_d3d12；**在 Linux 上完整编译需后续**：条件化跳过 D3D12、用 window_x11 替换 Window::Create、Vulkan surface 从 Win32 抽出。在仅有本仓库 Win 路径时，请把 ENGINE_LINUX_VK 当**文档开关**，勿期望 
inja 立即通过。

## 建议冒烟（有 Linux CI / 实机时）

`ash
cmake --build build-linux -j
ctest --test-dir build-linux -R unit --output-on-failure
# 有显示与驱动时：
# ./build-linux/samples/.../sample_01_clear --backend=vulkan
# 无 DISPLAY 的 SSH 会话：
# xvfb-run -a ./build-linux/samples/.../sample_01_clear --backend=vulkan
`

无 GPU / 无 X11 显示：单测与 headless 可先跑；交互清屏 **SKIP**，记录 CI 说明。

## 实机冒烟记录

| 日期 | 主机 | 用户 | 结果 | 说明 |
|---|---|---|---|---|
| 2026-08-18 | 172.30.2.200 | breton | **OK（部分）** | SSH 连通。uname：Ubuntu 24.04 系（7.0.0-28-generic x86_64）。已有 cmake 3.28.3、
inja 1.11.1、g++ 13.3、libx11-dev、/usr/include/X11、libvulkan-dev 1.3.275、ulkan-tools、pkg-config x11/vulkan。VULKAN_SDK **未设置**（系统头/库可用）。DISPLAY 在 SSH 会话为空；最小 X11 程序编译 **OK**，裸跑 XOpenDisplay 失败；xvfb-run 下开窗 **OK**。GPU：NVIDIA RTX A4000 + llvmpipe。**未做完整引擎 cmake/ninja**（Windows 主路径 / D3D12 未条件化，预期缺口）。Wayland：本波后置，未测。 |

缺口摘要（相对完整 Linux Vulkan clear）：

1. 引擎 Linux 全量构建（跳过 Win32/D3D12、工厂接 window_x11）尚未在该机验证。
2. VK_KHR_xlib_surface / swapchain clear 未接线 → TryX11ClearPathStub SKIP。
3. 交互显示需本机 DISPLAY 或 xvfb-run；VULKAN_SDK 可选但建议在文档化路径下显式设置。

## 已知缺口

1. **Window::Create 仍为 Win32** — Linux 桩未接入统一工厂。
2. **Vulkan surface 绑定 Win32** — 需平台层抽象后再接 X11（TryX11ClearPathStub）。
3. **Wayland** — **明确后置**；本波要求 X11。
4. **Vulkan Video** — 与 D3D12VA 对等验收后置；能力不足 SKIP。
5. **运行时全量冒烟** — 依赖自托管 Linux runner / 实机；本机 Win 开发机不替代。

## 相关

- [PLAN.md](PLAN.md) M18 · [VULKAN_PARITY.md](VULKAN_PARITY.md) · [KNOWN_GAPS.md](KNOWN_GAPS.md) G19
- ADR [0020](learn/adr/0020-windows-d3d12-vulkan-linux-vulkan.md) · [ADR 0036](learn/adr/0036-mega-w9-deepen.md) · QUIC [ADR 0031](learn/adr/0031-m19-quic-skip-msquic.md)
