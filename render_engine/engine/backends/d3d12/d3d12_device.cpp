#include "d3d12_device_internal.h"

namespace engine::rhi {

Result<std::unique_ptr<IDevice>> CreateD3D12Device(const DeviceDesc& desc) {
    auto device = std::make_unique<D3D12Device>();
    if (auto st = device->Init(desc); !st) {
        return Result<std::unique_ptr<IDevice>>::Fail(st);
    }
    return Result<std::unique_ptr<IDevice>>::Ok(std::unique_ptr<IDevice>(std::move(device)));
}

std::vector<GpuAdapterInfo> EnumerateD3D12Adapters() {
    std::vector<GpuAdapterInfo> out;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        return out;
    }
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 ad{};
        adapter->GetDesc1(&ad);
        GpuAdapterInfo info;
        info.index = static_cast<int>(i);
        info.software = (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        info.dedicated_memory_bytes = ad.DedicatedVideoMemory;
        info.discrete = !info.software && ad.DedicatedVideoMemory > (512ull << 20);
        char name_utf8[256]{};
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name_utf8, sizeof(name_utf8), nullptr,
                                                nullptr);
        info.name = name_utf8;
        out.push_back(std::move(info));
    }
    return out;
}

}  // namespace engine::rhi
