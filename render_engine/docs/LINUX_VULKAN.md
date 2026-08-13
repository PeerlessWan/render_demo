# Linux + Vulkan（M18）

当前仓库以 Windows 为主路径。状态：

- **Windows Vulkan（M17 加深）**：`backends/vulkan/vulkan_device.cpp` 已提供 Win32 surface + swapchain **清屏**路径（`ENGINE_WITH_VULKAN`，默认在检测到 `$VULKAN_SDK` 时 ON）。`CreateDevice(Backend::Vulkan)`：非 headless → `CreateVulkanDevice`；headless → headless device。
- `platform/linux/`：待落地（X11 必做，Wayland 目标内）
- Linux 构建：应关闭 D3D12 目标，仅链 Vulkan；surface 需换 X11/Wayland 扩展

验收前请实现 `window_x11` + 将 Vulkan Device 的 surface 创建从 Win32 抽出平台层，并保证 Sample 可 `--backend=vulkan` 清屏。
