# ADR 0035: Mega-W8 加深与尾巴完善边界

- 状态: Accepted（MS stub 等 → **[ADR 0036](0036-mega-w9-deepen.md)** 加深）
- 日期: 2026-08-17
- 关联: PLAN M27+ / Mega-W8、KNOWN_GAPS、ADR 0031（MsQuic）、ADR 0034、ADR 0036

> **修订注记（Mega-W9）：** 「MS 真 PSO 本波为 Feature stub」以 [ADR 0036](0036-mega-w9-deepen.md) 为准（D3D12 真 MS / VK 探测）。
## 背景

W7 后一次收口剩余可加深项、A 段尾巴、天气/无限海/浮力，并按旨并入 **C06 最小 VT** 与 **MsQuic 可选启用**。明确不加 HLOD/XR/材质节点图/蓝图。

## 决策（已落地口径）

1. **C02**：CPU `PackTileLightLists` → FrameCB；lit 按屏幕 tile 累加；`enable_tiled_lights`。
2. **C08**：meshlet cook + `CullMeshletsToIndirect`；MS 真 PSO 本波为 Feature stub（**Mega-W9 / ADR 0036 加深为真 MS**）。
3. **C06**：最小 VT 页表/物理缓存/请求/Sample stub（非 Nanite）。
4. **MsQuic**：探测存在则 Feature 可开；**不静默安装**；缺库 SKIP（ADR 0031 修订）。
5. **天气 / FFT 海 / 浮力**：WeatherSystem + 无限平铺 FFT + 探针浮力（thrust/flood）。
6. **动画/IES/Post**：TwoBoneIK、BlendSpace2D/mask/Notify、IES 文件 LUT、镜头畸变/脏点/眩光。
7. **C12/C16/2D**：VK 蒙皮、AssetHotReload、Path2D/九宫格/富文本/世界字。
8. **game_kit / editor**：脚本热重载、GK5 骨架、`IScriptHost` stub、editor_smoke_tests。

## 仍外置

HLOD、XR、材质节点图、蓝图、mac/移动、VT 全材质默认化、完整舰船模拟、气象数值模式。

## 后果

- 单测：`test_m29`–`test_m31`、game_kit_tests、editor_smoke_tests。
- 文档：DOING Mega-W8 收口；KNOWN_GAPS 对应行更新为部分/已落地。
