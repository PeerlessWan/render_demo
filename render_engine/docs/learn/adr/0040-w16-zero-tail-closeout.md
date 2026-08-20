# ADR 0040: W16 零尾巴收口

- 状态: Accepted
- 日期: 2026-08-19
- 关联: ADR 0039、KNOWN_GAPS §3、PLAN W12–W15、DOING_UNDO_TODO

## 背景

W12–W15 留下假壳（FSR 调 bilinear、GPU 粒子 CPU 冒充、Wayland 探测后 X11 present、QUIC session-stub、VK bindless 无热路径等）。用户要求 **别留尾巴**：ADR 0039 范围内每项必须关门。

## 决策

1. **二选一**：真路径达标，或诚实 Unavailable / 删假壳；禁止「stand-in until wired」「session-stub Ok」「gpu-contract-cpu-integrate」挂在产品默认路径。
2. **验收矩阵**（Ok / SKIP / 禁止）：

| 项 | Ok | SKIP | 禁止 |
|---|---|---|---|
| 超分 | `name()`=`builtin_bilinear`；有真 FFX/NGX 时才 `fsr2`/`dlss` | 无 SDK | 假类 `name=fsr2` 内部 bilinear |
| VK bindless 热路径 | indexing + `bindless_hot_path` → albedo pad 映射 | 无 indexing | Feature 开但 pad 恒 -1 |
| GPU 粒子 | CS integrate 路径名含 `gpu-cs`；否则 `cpu-fallback` 且不宣称 GPU | 无 CS 能力关 Feature | Feature 开却只 CPU 且名含 gpu-contract |
| Wayland | xdg-shell + `VK_KHR_wayland_surface` present | 无 Wayland / 无协议 | 探测成功静默 X11 却记 Wayland Ok |
| glTF 角色 | 多 prim；蒙皮多 mesh **多 draw 保 skin** | 无资产 | 合并清 skin；只 prim0 |
| QUIC | 链接 MsQuic 且 SendReliable 真 | 无库 / 未链接 | Connect Ok + session-stub |
| meshoptimizer Prefer | `#if ENGINE_WITH_MESHOPTIMIZER` 调库 | 无库回落 AABB | Prefer 永不调库却保留 API 谎言 |
| VT 近默认 | Sandbox/EffectTuning 可开 | Feature 关 | 文档写近默认却永不演示 |
| 软影 | API 名与实现一致 | 无 RT | 称「半分辨率」却仅 compose factor |

3. **外置不变**：Nanite / 真 DDGI / 引擎内复制 / mac / C17 / game_kit。
4. **里程碑**：本波称 **W16**；收口后看板标 W12–W16 **已收口**，无「后续 FSR/Wayland」残留。

## 后果

- 优点：水位诚实；Sandbox / Feature / 路径名可对账。
- 代价：无 vendor 时能力面变窄（诚实 SKIP），不再用假 Ok 充数。

## 学习提示

1. 假壳比缺能力更伤信任。
2. Wayland 与 X11 并存；成功创建 Wayland 窗口则不得 silently 换 X11 present。
3. 蒙皮多 mesh：多 draw，禁止合并后清 `has_skin`。
