# ADR 0033: M27/W6 场景规模加深边界

- 状态: Accepted
- 日期: 2026-08-17
- 关联: PLAN M27+ / W6、KNOWN_GAPS C08–C12/C16、ProbeVolume、AnimationStateMachine、ShaderHotReload

## 背景

解封后 W4/W5 已收口画质与平台/媒体债。W6 目标是场景规模向加深，但明确不追 VT/Nanite 全家桶、完整 Mesh Shader 产品管线或 DDGI 全实现。

## 决策

1. **GI**：`ProbeVolume::RefineDensity` 加密 CPU 探针网格；仍非 GPU DDGI。
2. **水面（C09）**：`AnimateWaterPatch` / `BuildAnimatedWaterPatchMesh` 提供 Gerstner 式高度与法线；非完整 FFT 海洋。
3. **混合（C10）**：`AnimationStateMachine::SampleBlend` 多 clip 权重线性混合骨骼矩阵。
4. **GPU 蒙皮（C12）**：`GpuSkinningAvailable` + `SkinVerticesGpuDispatchStub`（Feature `gpu_skinning`）；默认走 CPU 蒙皮。
5. **Meshlet（C08）**：`MeshletPathAvailable`（Feature `meshlet`）门控；默认仍 Indirect Cull。
6. **热重载（C16）**：`Poll` 置位 `NeedsPsoRebuild`；宿主 `ConsumePsoRebuildRequest` 后重建相关 PSO。

## 不做

- C06 VT、C07 HLOD、完整 Mesh Shader PSO、MsQuic、editor/game_kit。

## 后果

- 单测见 `tests/unit/test_m27.cpp`。
- 文档：KNOWN_GAPS / DOING / PLAN §6 同步为 W4–W6 收口。
