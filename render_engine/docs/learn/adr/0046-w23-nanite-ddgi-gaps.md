# ADR 0046: Mega-W23 缺口收口 + Nanite-like + 真 DDGI

- 状态: Accepted（**W23 已收口**；见 [DOING_UNDO_TODO.md](../../DOING_UNDO_TODO.md)）
- 日期: 2026-08-20
- 关联: ADR 0045、ENGINE_VS_MAINSTREAM、THIRD_PARTY、gi/README、gpu_driven

## 背景

W22 将桌面 Forward+ 渲染内核对标 Godot 自评 ≈100%。用户要求补足后处理/RT/GPU Driven/材质粒子/物理缺口，并解冻 **Nanite-like** 与 **真 NVIDIA DDGI（RTXGI）**；DLSS(NGX) 与 RTXGI SDK **同一波本地安装**。

## 决策

1. **解冻**：撤销 ADR 0045「仍不做 Nanite / 真 NVIDIA DDGI」。
2. **Nanite**：交付 **VirtualGeometry（Nanite-like）**——层次 cluster、屏幕误差 LOD、驻留流式、GPU cull、Indirect 主路径。**不宣称 UE Nanite**。
3. **真 DDGI**：可选 **NVIDIA RTXGI**（`BindGiGpuDevice` → `TryCreateRtxgiVolume`）；无 SDK → 诚实 SKIP；产品默认仍 **CascadeGi**。禁止 CascadeGi 冒充 DDGI。
4. **W23-sdk 前置**：`tools/fetch_nvidia_ngx_rtxgi.ps1` 一并准备 `third_party/ngx` + `third_party/rtxgi`；CMake `ENGINE_WITH_NGX` / `ENGINE_WITH_RTXGI`；专有二进制不进 git。
5. **本波还做**：Post（GTAO/SSR/fog box/LUT/FXAA）；RT 半分辨率软影/反射产品化一步；lit detail/triplanar；粒子 mesh 碰撞；Joints/Vehicle/可破坏代理。
6. **仍不做**：Lumen、Frame Generation、Intel XeSS、mac/Metal、引擎内脚本/复制、C17、宣称商标级 Nanite。

## 波次

| 波 | 内容 |
|---|---|
| W23-sdk | NGX + RTXGI 本地 drop-in + CMake |
| W23a | 后处理质量 |
| W23b | RT 产品化一步（DXR；VK stub） |
| W23c | VirtualGeometry |
| W23d | RTXGI Bind/Create |
| W23e | 材质 GPU + 粒子 |
| W23f | 物理关节/载具/破碎 |
| W23g | 文档/单测/水位 |

## 后果

- 优点：UE 向几何/GI 方向抬一档；厂商 SDK 与 Upscaler 同纪律。
- 代价：无 NVIDIA 本机包时 RTXGI/DLSS 仅 SKIP；VirtualGeometry 非完整 UE 管线。

## 收口备注（2026-08-20）

- SDK：`fetch_nvidia_ngx_rtxgi.ps1`；`ENGINE_WITH_RTXGI`；README drop-in。
- VirtualGeometry：层次 DAG + 屏幕误差 LOD + 驻留 + Indirect cull。
- RTXGI：`BindGiGpuDevice` / `TryCreateRtxgiVolume`（无头 → nullptr）。
- Post：GTAO / FXAA / color grading / fog box / SSR roughness fade。
- lit：detail + triplanar；粒子 mesh 碰撞 + 子发射树。
- RT：`TryProductSoftShadowMask` / `TryHalfResRtReflectionCompose`。
- Phys：Joints / Ragdoll / Vehicle / Shatter（Jolt）。
- 单测：**215 passed / 0 failed**。
