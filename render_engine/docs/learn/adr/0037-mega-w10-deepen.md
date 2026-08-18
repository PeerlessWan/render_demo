# ADR 0037: Mega-W10 全主题加深（半成品收紧、尾巴、大场景、人物、学习轨）

- 状态: Accepted
- 日期: 2026-08-18
- 关联: PLAN Mega-W10、KNOWN_GAPS、ADR 0029/0036、LINUX.md

## 背景

Mega-W9 收口后继续加深：第1层半成品可演示收紧、第2层引擎尾巴、CC0 大地形、人物 possess 走跳与演示级服装、Linux 实机冒烟、game_kit GK、学习 Sample 37–39。

## 决策

1. **C02**：灯上限 32；Z-slices 4；tile CS/CPU 对齐；不做 deferred G-buffer。
2. **C06/C07**：VT lit 采样 +「近默认」Sandbox 开关；HLOD bake 落盘；**不做 Nanite**。
3. **GI/RT**：DDGI-lite（探针邻域/级联，非 NVIDIA DDGI）；半分辨率全屏软阴影 Feature。
4. **MS/Bindless**：D3D12 MS/bindless 可开；VK MS 尽力；VK bindless SKIP。
5. **Linux**：X11+VK 实机冒烟；Wayland **文档后置**；密码不入仓。
6. **人物/服装**：`possess_character` 默认自由视角；开则走/跳/碰撞；披风裙摆 SoftBody（修订 ADR 0029：允许演示级挂接，禁止 DCC 管线）。
7. **大场景**：CC0 高度图 2k–4k + ChunkStream；大原图可 gitignore + 拉取脚本。
8. **学习轨**：`37_clothing` / `38_large_terrain` / `39_w10_deepen`。
9. **仍外置**：Nanite、真 NVIDIA DDGI、节点图、蓝图、XR、mac/移动、C17 实装。

## 后果

- 单测 `test_m34` / `test_m35`；看板 Mega-W10。
- Linux 冒烟记录只写主机/用户名与结果。
