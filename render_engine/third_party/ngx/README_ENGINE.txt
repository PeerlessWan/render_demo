NVIDIA NGX / DLSS vendor drop 鈥?ADR 0046 (Mega-W23).

1. Place official NGX/DLSS SDK here so CMake finds nvsdk_ngx.h
   (include/nvsdk_ngx.h or nvsdk_ngx.h at root).
2. Runtime: nvngx_dlss.dll / nvngx.dll on PATH or next to the exe.
3. cmake: ENGINE_NGX_DIR / ENGINE_WITH_NGX (auto-ON when header present).
4. Product chain: BindUpscalerGpuDevice 鈫?CreateUpscaler 鈫?DLSS鈫扚SR鈫抌ilinear.
5. Without evaluate/runtime 鈫?honest SKIP / builtin_bilinear.

Fetch: tools/fetch_nvidia_ngx_rtxgi.ps1 -NgxSource <official_dir_or_zip>
Do not commit proprietary .dll/.lib into git.
