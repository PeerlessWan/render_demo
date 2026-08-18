# Linux + Vulkan（M18 · Mega-W9 加深）

> 当前仓库 **以 Windows 为主路径**。本页给出 Linux（X11）上构建 Vulkan 的步骤与 CMake 开关；**运行时冒烟视自托管 CI 机**，本波不承诺本机 Windows 树能交叉编出完整 Linux 二进制。

## 现状

| 项 | 状态 |
|---|---|
| Windows Vulkan（M17） | `backends/vulkan/vulkan_device.cpp`：Win32 surface + swapchain（`ENGINE_WITH_VULKAN`） |
| `engine/platform/win32/` | 已实现 `Window::Create`（Win32 + headless） |
| `engine/platform/linux/` | **Mega-W9**：`include/engine/platform/linux/window_x11.h` + `platform/linux/window_x11.cpp` 桩；`#ifdef __linux__` 下 headless Ok / 真窗口 Unavailable |
| D3D12 | Linux **不做**；CMake 应只开 Vulkan |
| X11 clear 路径 | 文档 + `TryX11ClearPathStub`（`VK_KHR_xlib_surface` 未接线 → SKIP） |

旧占位页 [LINUX_VULKAN.md](LINUX_VULKAN.md) 已并入本页。

## X11 clear 路径（Mega-W9）

目标链路（有 Linux 显示 + Vulkan 驱动时）：

1. `CreateX11WindowStub` → 真实现改为 `XOpenDisplay` + `XCreateSimpleWindow`（当前非 headless 为 Unavailable）。  
2. Vulkan `VK_KHR_xlib_surface` / `xcb` 创建 surface。  
3. `sample_01_clear --backend=vulkan` 清屏一帧。

本波验收：

- 头文件桩可被 Windows 树包含；`__linux__` 上 headless 返回 `Ok("x11-headless-stub")`。  
- `TryX11ClearPathStub` 诚实 `Unavailable SKIP`，直至 surface 接线。  
- `ENGINE_LINUX_VK=ON` 仅声明意图并打印缺口，**不**静默安装 X11/Vulkan。

## 依赖（主机包，引擎不安装）

```bash
# Debian/Ubuntu 示例（自行安装，勿在引擎脚本里静默装）
sudo apt install build-essential cmake ninja-build \
  libx11-dev libxi-dev libxkbcommon-dev \
  vulkan-sdk   # 或发行版 vulkan-headers / libvulkan-dev + LunarG SDK
```

- **Vulkan SDK**：设置 `VULKAN_SDK`（或 `ENGINE_VULKAN_SDK`）指向含 `Include/vulkan/vulkan.h` 的前缀。  
- **X11**：本波目标窗口后端；`libX11` + 输入相关头。  
- **Wayland**：明确 **后置**（见下方缺口）。  
- **OpenSSL（可选 HTTPS）**：见 [THIRD_PARTY.md](THIRD_PARTY.md)；`ENGINE_WITH_OPENSSL`，**引擎不安装 OpenSSL**。

## CMake 标志

```bash
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENGINE_WITH_VULKAN=ON \
  -DENGINE_VULKAN_SDK="$VULKAN_SDK" \
  -DENGINE_LINUX_VK=ON \
  -DENGINE_BUILD_SAMPLES=ON \
  -DENGINE_BUILD_LEARN_SAMPLES=OFF \
  -DENGINE_WITH_OPENSSL=OFF
# 若本机已有 OpenSSL 且要测 HTTPS：
#   -DENGINE_WITH_OPENSSL=ON  （可选 -DOPENSSL_ROOT_DIR=...）
# 切勿在配置脚本里 apt/brew 安装 OpenSSL / MsQuic
```

| 选项 | 含义 |
|---|---|
| `ENGINE_WITH_VULKAN` | 编真实 Vulkan `IDevice`（需 SDK） |
| `ENGINE_LINUX_VK` | **声明意图**：Linux Vulkan + X11 clear；ON 时打印状态与缺口；在非 Linux 主机上仅为文档/开关预留 |
| `ENGINE_WITH_OPENSSL` | 系统 OpenSSL 已 `find_package` 成功时启用 HTTPS；失败则 HTTPS `Unavailable` |

Windows 树当前仍链 `platform/win32` 与 `engine_d3d12`；**在 Linux 上完整编译需后续**：条件化跳过 D3D12、用 `window_x11` 替换 `Window::Create`、Vulkan surface 从 Win32 抽出。在仅有本仓库 Win 路径时，请把 `ENGINE_LINUX_VK` 当 **文档开关**，勿期望 `ninja` 立即通过。

## 建议冒烟（有 Linux CI 机时）

```bash
cmake --build build-linux -j
ctest --test-dir build-linux -R unit --output-on-failure
# 有显示与驱动时：
# ./build-linux/samples/.../sample_01_clear --backend=vulkan
```

无 GPU / 无 X11 显示：单测与 headless 可先跑；交互清屏 **SKIP**，记入 CI 说明。

## 已知缺口

1. **`Window::Create` 仍为 Win32** — Linux 桩未接入统一工厂。  
2. **Vulkan surface 绑定 Win32** — 需平台层抽象后再接 X11（`TryX11ClearPathStub`）。  
3. **Wayland** — 目标内、本波不做。  
4. **Vulkan Video** — 与 D3D12VA 对等验收后置；能力不足 SKIP。  
5. **运行时冒烟** — 依赖自托管 Linux runner；本机 Win 开发机不替代。

## 相关

- [PLAN.md](PLAN.md) M18 · [VULKAN_PARITY.md](VULKAN_PARITY.md) · [KNOWN_GAPS.md](KNOWN_GAPS.md) G19  
- ADR [0020](learn/adr/0020-windows-d3d12-vulkan-linux-vulkan.md) · [ADR 0036](learn/adr/0036-mega-w9-deepen.md) · QUIC [ADR 0031](learn/adr/0031-m19-quic-skip-msquic.md)
