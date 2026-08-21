NVIDIA NGX / DLSS vendor drop — ADR 0046 / 0048 (Mega-W23/W25).

1. Place official NGX/DLSS SDK here so CMake finds nvsdk_ngx.h
   (include/nvsdk_ngx.h or nvsdk_ngx.h at root).
2. Runtime: nvngx_dlss.dll / nvngx.dll on PATH or next to the exe.
3. cmake: ENGINE_NGX_DIR / ENGINE_WITH_NGX (auto-ON when header present).
4. Product chain: BindUpscalerGpuDevice → CreateUpscaler → DLSS→FSR→bilinear.
5. Without evaluate/runtime → honest SKIP / builtin_bilinear.
6. W25: place matching .lib under lib/ so CMake sets ENGINE_NGX_EVALUATE_LINKED
   → VendorUpscaler ready_=true and Upscale evaluate dispatch.

Fetch: tools/fetch_nvidia_ngx_rtxgi.ps1 -NgxSource <official_dir_or_zip>
Do not commit proprietary .dll/.lib into git.
