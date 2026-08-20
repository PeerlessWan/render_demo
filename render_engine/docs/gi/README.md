# GI

> **产品默认：** CascadeGi（Godot SDFGI 风格）。  
> **可选真 DDGI：** NVIDIA RTXGI（ADR 0046）— `BindGiGpuDevice` → `TryCreateRtxgiVolume`；无 SDK → 诚实 SKIP。  
> **不是** Lumen。禁止把 CascadeGi 改名 DDGI。

## API

| 路径 | 说明 |
|---|---|
| `cascade_gi.h` | 多 cascade + SDF 遮挡 + 漏光抑制 |
| `probe_volume.h` | CPU 探针网格 → irradiance atlas |
| `rtxgi.h` | 可选 RTXGI（真 DDGI）；`ENGINE_WITH_RTXGI` |
| `reflection_probe.h` / `lightmap.h` / `scene_capture.h` | 共存路径 |

## SDK 安装

```text
tools/fetch_nvidia_ngx_rtxgi.ps1 -NgxSource <ngx> -RtxgiSource <rtxgi>
→ third_party/ngx + third_party/rtxgi
```

见 [THIRD_PARTY.md](../THIRD_PARTY.md)、[ADR 0046](../learn/adr/0046-w23-nanite-ddgi-gaps.md)。
