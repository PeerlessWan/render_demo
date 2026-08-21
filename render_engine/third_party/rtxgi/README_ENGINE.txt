NVIDIA RTXGI (true DDGI) vendor drop — ADR 0046 / 0048 (Mega-W23/W25).

1. Place official RTXGI SDK here. CMake probes for any of:
   include/ddgi/DDGIVolume.h
   include/rtxgi/ddgi/DDGIVolume.h
   ddgi/DDGIVolume.h
   RTXGI.h / rtxgi.h
2. cmake: ENGINE_RTXGI_DIR / ENGINE_WITH_RTXGI (auto-ON when header present).
3. Product: BindGiGpuDevice → TryCreateRtxgiVolume → irradiance atlas.
4. Without SDK → SKIP; CascadeGi remains the default product GI.
5. Never rename CascadeGi as DDGI.
6. W25: place matching .lib so CMake sets ENGINE_RTXGI_EVALUATE_LINKED
   → ready()=true and Update writes atlas.

Fetch: tools/fetch_nvidia_ngx_rtxgi.ps1 -RtxgiSource <official_dir_or_zip>
Do not commit proprietary .dll/.lib into git.
