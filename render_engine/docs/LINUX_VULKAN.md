# Linux + Vulkan（M18）

当前仓库以 Windows/D3D12 为主路径。M18 骨架状态：

- `platform/linux/`：待落地（X11 必做，Wayland 目标内）
- `backends/vulkan/`：待落地；`CreateDevice(Backend::Vulkan)` 现返回 `Unavailable`
- 构建：Linux 上应关闭 D3D12 目标，仅链 Vulkan

验收前请实现 `window_x11` + Vulkan Device/Swapchain，并保证 Sample 可 `--backend=vulkan` 清屏。
