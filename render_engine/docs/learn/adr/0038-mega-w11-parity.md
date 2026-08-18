# ADR 0038: Mega-W11 拉齐各端（引擎 only）

- 状态: Accepted
- 日期: 2026-08-18
- 关联: VULKAN_PARITY、LINUX、ADR 0037、KNOWN_GAPS

## 背景

Mega-W10 后继续「拉齐各端」：Win Vulkan 热路径对齐 D3D12；Linux 可编 + X11/VK clear；CC0 人物 glTF。`game_kit/` / `editor/` 由其他会话负责，本 ADR **禁止改动**。

## 决策

1. **VK tile light CS**：真 Dispatch 或同形缓冲；缺 shader → Unavailable SKIP。
2. **GPU 蒙皮**：主 `IDevice` 上 D3D12+VK 可用路径。
3. **Mesh Shader / RT**：有扩展则最小示范，否则诚实 SKIP。
4. **Bindless**：VK 能开则最小热路径；不能则文档钉死 SKIP（不假装）。
5. **Linux**：UNIX CMake 跳过 D3D12；X11 + `VK_KHR_xlib_surface`（或 xcb）；实机 clear；Wayland 仍后置。
6. **人物**：CC0 glTF 入 `content/characters/`；possess/37 优先加载，失败回退胶囊。
7. **C4**：默认松闸 PASS；紧闸现状文档化。
8. **仍外置**：Nanite、真 NVIDIA DDGI、节点图、蓝图、XR、mac、C17；**不改 game_kit/editor**。

## 后果

- 单测 `test_m36`；更新 VULKAN_PARITY / LINUX / DOING。
- SSH 冒烟记录不含密码。
