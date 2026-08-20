# ADR 0042: W18 半落地加深

- 状态: Accepted
- 日期: 2026-08-20
- 关联: ADR 0041、KNOWN_GAPS §4（C02/C06/C07/C08/C14/C16/G13/G16）

## 背景

W17 收口后仍有一批「API/着色器已有、主路径半落地」项。用户要求：**引擎内仍可加深的半落地都做**。

## 决策

1. **本波做**：
   - C02：D3D12/VK light tile cull 真 CS Dispatch（失败回落 Simulate）
   - C14/G13：WorldText/Path2D 可提交 lit/quad mesh（Sandbox 可感）
   - C06：VT feedback 解码入口 + 多请求近场（诚实 CPU/GPU 路径名）
   - G16：软影 compose 加深并接线 Sandbox（无 RT → SKIP）
   - C08：主路径/Sandbox Mesh Shader 示范（无 Tier/EXT → SKIP）
   - C16：`TryCompileHlslWithDxc` 写出 `.cso` + 正确 entry
   - C07：HLOD 距离切换进 Sandbox + bake 落盘加深
2. **本波不做**：Nanite、真 DDGI、FFX/NGX、MsQuic 真 API、SVG 布尔、复制、mac、C17。
3. **验收**：路径名诚实；缺字节码/扩展 → Unavailable SKIP；单测不破；Sandbox 可感。

## 后果

半落地债收一档；仍非 UE 产品级。

## 收口备注（2026-08-20）

- C02：**D3D12** 真 CS one-shot + readback（`gpu-cs` / `cpu-simulate`）；**VK** 同路径（W19 SSBO+UBO Dispatch）。
- Sandbox：WorldText/Path2D lit slots 13/14；HLOD slot15；软影×sun；MS 探测；VT packed；dxc 写 vs/ps cso。
- 单测：187 passed / 0 failed。
