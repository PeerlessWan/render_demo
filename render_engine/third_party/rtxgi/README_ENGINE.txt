NVIDIA RTXGI (true DDGI) vendor drop 鈥?ADR 0046 (Mega-W23).

1. Place official RTXGI SDK here. CMake probes for any of:
   include/ddgi/DDGIVolume.h
   include/rtxgi/ddgi/DDGIVolume.h
   ddgi/DDGIVolume.h
   RTXGI.h / rtxgi.h
2. cmake: ENGINE_RTXGI_DIR / ENGINE_WITH_RTXGI (auto-ON when header present).
3. Product: BindGiGpuDevice 鈫?TryCreateRtxgiVolume 鈫?irradiance atlas.
4. Without SDK 鈫?SKIP; CascadeGi remains the default product GI.
5. Never rename CascadeGi as DDGI.

Fetch: tools/fetch_nvidia_ngx_rtxgi.ps1 -RtxgiSource <official_dir_or_zip>
Do not commit proprietary .dll/.lib into git.
