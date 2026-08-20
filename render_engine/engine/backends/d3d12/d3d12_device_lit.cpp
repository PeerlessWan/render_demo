#include "d3d12_device_internal.h"

namespace engine::rhi {

Status D3D12Device::SetupLitMesh(const LitMeshShaders& shaders) {
    WaitGpu();
    auto vs = ReadFileBytes(shaders.vs_dxil);
    if (!vs) {
        return vs.status();
    }
    auto ps = ReadFileBytes(shaders.ps_dxil);
    if (!ps) {
        return ps.status();
    }
    auto shadow_vs = ReadFileBytes(shaders.shadow_vs_dxil);
    if (!shadow_vs) {
        return shadow_vs.status();
    }
    auto shadow_ps = ReadFileBytes(shaders.shadow_ps_dxil);
    if (!shadow_ps) {
        return shadow_ps.status();
    }

    // Lit root: CBV b0, CBV b1, table t0..t8 + t10..t12 (pixel), root SRV t9 (instances VS).
    // t6 = IBL specular prefilter; t10 = reflection probe; t11 = GI atlas; t12 = soft shadow.
    // SM 6.6 ResourceDescriptorHeap needs Root Signature 1.1 + DIRECTLY_INDEXED.
    // CBV/SRV data is rewritten every frame (UPLOAD + shadow maps) → must NOT use DATA_STATIC.
    // Two ranges so t9 stays a root SRV (no overlap with the descriptor table).
    D3D12_DESCRIPTOR_RANGE1 srv_ranges[2]{};
    srv_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_ranges[0].NumDescriptors = 9;  // t0..t8
    srv_ranges[0].BaseShaderRegister = 0;
    srv_ranges[0].RegisterSpace = 0;
    srv_ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    srv_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    srv_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_ranges[1].NumDescriptors = 3;  // t10..t12
    srv_ranges[1].BaseShaderRegister = 10;
    srv_ranges[1].RegisterSpace = 0;
    srv_ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    srv_ranges[1].OffsetInDescriptorsFromTableStart = 10;

    D3D12_ROOT_PARAMETER1 lit_params[4]{};
    lit_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    lit_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    lit_params[0].Descriptor.ShaderRegister = 0;
    lit_params[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    lit_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    lit_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    lit_params[1].Descriptor.ShaderRegister = 1;
    lit_params[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    lit_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lit_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    lit_params[2].DescriptorTable.NumDescriptorRanges = 2;
    lit_params[2].DescriptorTable.pDescriptorRanges = srv_ranges;
    lit_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    lit_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    lit_params[3].Descriptor.ShaderRegister = 9;
    lit_params[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;

    D3D12_STATIC_SAMPLER_DESC lit_samplers[2]{};
    lit_samplers[0].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    lit_samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    lit_samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    lit_samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    lit_samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    lit_samplers[0].MaxAnisotropy = 1;
    lit_samplers[0].MinLOD = 0.f;
    lit_samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    lit_samplers[0].ShaderRegister = 0;
    lit_samplers[0].RegisterSpace = 0;
    lit_samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    lit_samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    lit_samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    lit_samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    lit_samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    lit_samplers[1].MaxAnisotropy = 1;
    lit_samplers[1].MinLOD = 0.f;
    lit_samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    lit_samplers[1].ShaderRegister = 1;
    lit_samplers[1].RegisterSpace = 0;
    lit_samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC lit_vrs{};
    lit_vrs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    lit_vrs.Desc_1_1.NumParameters = 4;
    lit_vrs.Desc_1_1.pParameters = lit_params;
    lit_vrs.Desc_1_1.NumStaticSamplers = 2;
    lit_vrs.Desc_1_1.pStaticSamplers = lit_samplers;
    lit_vrs.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                     D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&lit_vrs, &sig, &err);
    if (FAILED(hr)) {
        const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
        return Status::Fail(std::string("Lit root sig failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                        IID_PPV_ARGS(&lit_root_));
    if (FAILED(hr)) {
        return Status::Fail("Create lit root signature failed");
    }

    D3D12_INPUT_ELEMENT_DESC lit_layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lit_pso{};
    lit_pso.pRootSignature = lit_root_.Get();
    lit_pso.VS = {vs->data(), vs->size()};
    lit_pso.PS = {ps->data(), ps->size()};
    lit_pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    lit_pso.SampleMask = UINT_MAX;
    lit_pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // NONE: matches Vulkan lit and avoids hollow untextured cubes from winding/viewport
    // mismatch. Floor slab mitigations are SV_ClipDistance + PS discard (not backface cull).
    // Ground plane indices are CCW from +Y (see Sandbox slot4 upload).
    lit_pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    lit_pso.RasterizerState.FrontCounterClockwise = TRUE;
    // Keep depth clip on: Perspective is Z∈[0,1]. DepthClip=FALSE clamped near-floor
    // depth to 0 and hid the debug grid under a featureless white plane.
    lit_pso.RasterizerState.DepthClipEnable = TRUE;
    lit_pso.DepthStencilState.DepthEnable = TRUE;
    lit_pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    lit_pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    lit_pso.InputLayout = {lit_layout, 3};
    lit_pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lit_pso.NumRenderTargets = 1;
    // Lit goes to HDR scene color; tonemap resolves to the LDR swapchain.
    lit_pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    lit_pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    lit_pso.SampleDesc.Count = 1;
    // W20 hot-reload: create into temps so a failed Create keeps the previous lit PSO.
    ComPtr<ID3D12PipelineState> new_lit_pso;
    hr = device_->CreateGraphicsPipelineState(&lit_pso, IID_PPV_ARGS(&new_lit_pso));
    if (FAILED(hr)) {
        return Status::Fail(
                "Create lit PSO failed: " + HrToString(hr) +
                " (often SM6.6 bindless PS vs stale binary/root-sig; rebuild Debug+Release)");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lit_pso_tr = lit_pso;
    lit_pso_tr.BlendState.AlphaToCoverageEnable = FALSE;
    lit_pso_tr.BlendState.RenderTarget[0].BlendEnable = TRUE;
    lit_pso_tr.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    lit_pso_tr.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    lit_pso_tr.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    lit_pso_tr.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
    lit_pso_tr.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    lit_pso_tr.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    lit_pso_tr.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    // Clamp rather than clip: near-plane cuts on alpha-blended glass became a floating
    // white slab that appeared/disappeared with the Transparent profiler pass.
    lit_pso_tr.RasterizerState.DepthClipEnable = FALSE;
    lit_pso_tr.DepthStencilState.DepthEnable = TRUE;
    lit_pso_tr.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    lit_pso_tr.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ComPtr<ID3D12PipelineState> new_lit_pso_tr;
    hr = device_->CreateGraphicsPipelineState(&lit_pso_tr, IID_PPV_ARGS(&new_lit_pso_tr));
    if (FAILED(hr)) {
        return Status::Fail("Create transparent lit PSO failed: " + HrToString(hr));
    }
    lit_pso_ = std::move(new_lit_pso);
    lit_pso_transparent_ = std::move(new_lit_pso_tr);

    // Shadow root: CBV b0 + CBV b1 only.
    D3D12_ROOT_PARAMETER shadow_params[2]{};
    shadow_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadow_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    shadow_params[0].Descriptor.ShaderRegister = 0;
    shadow_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadow_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    shadow_params[1].Descriptor.ShaderRegister = 1;

    D3D12_ROOT_SIGNATURE_DESC shadow_rs{};
    shadow_rs.NumParameters = 2;
    shadow_rs.pParameters = shadow_params;
    shadow_rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    sig.Reset();
    err.Reset();
    hr = D3D12SerializeRootSignature(&shadow_rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
        const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
        return Status::Fail(std::string("Shadow root sig failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                        IID_PPV_ARGS(&shadow_root_));
    if (FAILED(hr)) {
        return Status::Fail("Create shadow root signature failed");
    }

    // Depth-only: POSITION from LitVertex stride (pos+normal+uv); no color RT.
    D3D12_INPUT_ELEMENT_DESC shadow_layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadow_pso{};
    shadow_pso.pRootSignature = shadow_root_.Get();
    shadow_pso.VS = {shadow_vs->data(), shadow_vs->size()};
    // Depth-only with NumRenderTargets=0: omit color PS (asset still required above).
    (void)shadow_ps;
    shadow_pso.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    shadow_pso.SampleMask = UINT_MAX;
    shadow_pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    shadow_pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    shadow_pso.RasterizerState.FrontCounterClockwise = TRUE;
    shadow_pso.RasterizerState.DepthClipEnable = TRUE;
    // Integer depth-bias units (D32_FLOAT). Vulkan uses depthBiasConstantFactor=2.5
    // (r-scaled units, not a literal copy of 1500) + same slope=2.0.
    shadow_pso.RasterizerState.DepthBias = 1500;
    shadow_pso.RasterizerState.SlopeScaledDepthBias = 2.0f;
    shadow_pso.RasterizerState.DepthBiasClamp = 0.f;
    shadow_pso.DepthStencilState.DepthEnable = TRUE;
    shadow_pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadow_pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadow_pso.InputLayout = {shadow_layout, 1};
    shadow_pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadow_pso.NumRenderTargets = 0;
    shadow_pso.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadow_pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadow_pso.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> new_shadow_pso;
    hr = device_->CreateGraphicsPipelineState(&shadow_pso, IID_PPV_ARGS(&new_shadow_pso));
    if (FAILED(hr)) {
        return Status::Fail("Create shadow PSO failed: " + HrToString(hr));
    }
    shadow_pso_ = std::move(new_shadow_pso);

    if (!dsv_) {
        if (auto st = CreateDepthBuffer(); !st) {
            return st;
        }
    }
    if (auto st = CreateShadowMap(); !st) {
        return st;
    }
    if (auto st = CreateLitAlbedoTexture(); !st) {
        return st;
    }
    if (auto st = CreateLitOrmTexture(); !st) {
        return st;
    }
    if (auto st = CreateLitAlbedoTextureSlot1(); !st) {
        return st;
    }
    if (auto st = CreateLitOrmTextureSlot1(); !st) {
        return st;
    }
    if (auto st = EnsureDefaultReflectionCubemap(); !st) {
        return st;
    }
    if (auto st = EnsureDefaultProbeGiAndSoftShadowTextures(); !st) {
        return st;
    }
    if (auto st = CreateLocalShadowMap(); !st) {
        return st;
    }
    if (auto st = CreateCubeMesh(); !st) {
        return st;
    }
    if (auto st = CreateLitConstantBuffers(); !st) {
        return st;
    }

    // Optional screen quads (paths may be empty).
    quad_ready_ = false;
    if (!shaders.quad_vs_dxil.empty() && !shaders.quad_ps_dxil.empty()) {
        if (auto st = SetupScreenQuads(shaders.quad_vs_dxil, shaders.quad_ps_dxil); !st) {
            return st;
        }
    }
    debug_ready_ = false;
    if (!shaders.debug_vs_dxil.empty() && !shaders.debug_ps_dxil.empty()) {
        if (auto st = SetupDebugLines(shaders.debug_vs_dxil, shaders.debug_ps_dxil); !st) {
            return st;
        }
    }

    lit_ready_ = true;
    LogInfo("Lit cube mesh + shadow map ready");
    return Status::Ok();
}

Status D3D12Device::SetFrameLighting(const FrameLighting& lighting) {
    if (!lit_ready_ || !frame_cb_) {
        return Status::Fail("SetupLitMesh not called");
    }
    struct FrameData {
        float view_proj[16];
        float cascade_vp[4][16];
        float sun_dir[3];
        float sun_intensity;
        float ambient[3];
        float shadow_bias;
        float sun_color[3];
        float specular_power;
        float eye[3];
        float enable_shadow;
        float cascade_splits[4];
        float cam_forward[3];
        float cascade_count;
        float tiles_per_row;
        float enable_ssao;
        float enable_taa;
        float local_count;
        float local_pos_range[32][4];
        float local_color_intensity[32][4];
        float local_spot[32][4];       // xyz=dir, w=cosOuter (-1 = point/omni)
        float local_spot_inner[32];    // cosInner for lights 0..31
        float local_shadow_vp[12][16];
        float enable_local_shadow;
        float local_shadow_bias;
        float local_shadow_count;
        float local_shadow_tiles;
        float prev_view_proj[16];
        float jitter_x;
        float jitter_y;
        float enable_reflection;
        float reflection_intensity;
        float enable_ibl;
        float ibl_intensity;
        float _pad_before_ies[2];  // HLSL: float4 g_local_ies starts 16-byte aligned
        float local_ies[32];  // C03/W7 profile id as float (0=off)
        // Mega-W10 C02: packed Forward+ tile×Z lists (must match lit_cube.hlsl).
        float enable_tiled_lights;
        float tile_grid_w;
        float tile_grid_h;
        float max_lights_per_tile;
        float z_slices;
        float z_near;
        float z_far;
        float _pad_z;
        float tile_light_count[128];
        float tile_light_index[1024];
        // W20 L0: must match lit_cube.hlsl FrameCB tail.
        float enable_probe_gi;
        float probe_gi_intensity;
        float probe_rgb_scale;
        float probe_nx;
        float probe_origin[3];
        float probe_ny;
        float probe_spacing[3];
        float probe_nz;
        float enable_soft_shadow_mask;
        float _pad_w20[3];
    } data{};
    std::memcpy(data.view_proj, lighting.view_proj.m.data(), sizeof(data.view_proj));
    for (int i = 0; i < 4; ++i) {
        std::memcpy(data.cascade_vp[i], lighting.cascade_view_proj[static_cast<std::size_t>(i)].m.data(),
                                sizeof(data.cascade_vp[i]));
    }
    data.sun_dir[0] = lighting.sun_direction.x;
    data.sun_dir[1] = lighting.sun_direction.y;
    data.sun_dir[2] = lighting.sun_direction.z;
    data.sun_intensity = lighting.sun_intensity;
    data.ambient[0] = lighting.ambient.r;
    data.ambient[1] = lighting.ambient.g;
    data.ambient[2] = lighting.ambient.b;
    data.shadow_bias = lighting.shadow_bias;
    data.sun_color[0] = lighting.sun_color.r;
    data.sun_color[1] = lighting.sun_color.g;
    data.sun_color[2] = lighting.sun_color.b;
    data.specular_power = lighting.specular_power;
    data.eye[0] = lighting.eye.x;
    data.eye[1] = lighting.eye.y;
    data.eye[2] = lighting.eye.z;
    data.enable_shadow = lighting.enable_shadows ? 1.f : 0.f;
    for (int i = 0; i < 4; ++i) {
        data.cascade_splits[i] = lighting.cascade_splits[static_cast<std::size_t>(i)];
    }
    data.cam_forward[0] = lighting.camera_forward.x;
    data.cam_forward[1] = lighting.camera_forward.y;
    data.cam_forward[2] = lighting.camera_forward.z;
    data.cascade_count = static_cast<float>(lighting.cascade_count);
    data.tiles_per_row = static_cast<float>(lighting.cascade_tiles_per_row);
    data.enable_ssao = lighting.enable_ssao ? 1.f : 0.f;
    data.enable_taa = lighting.enable_taa ? 1.f : 0.f;
    data.local_count = static_cast<float>(lighting.local_light_count);
    for (int i = 0; i < 32; ++i) {
        data.local_pos_range[i][0] = lighting.local_pos[static_cast<std::size_t>(i)].x;
        data.local_pos_range[i][1] = lighting.local_pos[static_cast<std::size_t>(i)].y;
        data.local_pos_range[i][2] = lighting.local_pos[static_cast<std::size_t>(i)].z;
        data.local_pos_range[i][3] = lighting.local_range[static_cast<std::size_t>(i)];
        data.local_color_intensity[i][0] = lighting.local_color[static_cast<std::size_t>(i)].r;
        data.local_color_intensity[i][1] = lighting.local_color[static_cast<std::size_t>(i)].g;
        data.local_color_intensity[i][2] = lighting.local_color[static_cast<std::size_t>(i)].b;
        data.local_color_intensity[i][3] = lighting.local_intensity[static_cast<std::size_t>(i)];
        data.local_spot[i][0] = lighting.local_spot[static_cast<std::size_t>(i)].x;
        data.local_spot[i][1] = lighting.local_spot[static_cast<std::size_t>(i)].y;
        data.local_spot[i][2] = lighting.local_spot[static_cast<std::size_t>(i)].z;
        data.local_spot[i][3] = lighting.local_spot[static_cast<std::size_t>(i)].w;
        data.local_spot_inner[i] = lighting.local_spot_inner[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < 12; ++i) {
        std::memcpy(data.local_shadow_vp[i],
                                lighting.local_shadow_vps[static_cast<std::size_t>(i)].m.data(),
                                sizeof(data.local_shadow_vp[i]));
    }
    // Compat: FrameLighting::local_shadow_vp remains tile 0.
    std::memcpy(data.local_shadow_vp[0], lighting.local_shadow_vp.m.data(),
                            sizeof(data.local_shadow_vp[0]));
    data.enable_local_shadow = lighting.enable_local_shadow ? 1.f : 0.f;
    data.local_shadow_bias = lighting.local_shadow_bias;
    data.local_shadow_count = static_cast<float>(lighting.local_shadow_count);
    data.local_shadow_tiles = static_cast<float>(lighting.local_shadow_tiles_per_row);
    std::memcpy(data.prev_view_proj, lighting.prev_view_proj.m.data(), sizeof(data.prev_view_proj));
    data.jitter_x = lighting.jitter_x;
    data.jitter_y = lighting.jitter_y;
    data.enable_reflection = lighting.enable_reflection_probe ? 1.f : 0.f;
    data.reflection_intensity = lighting.reflection_intensity;
    data.enable_ibl = lighting.enable_ibl ? 1.f : 0.f;
    data.ibl_intensity = lighting.ibl_intensity;
    for (int i = 0; i < 32; ++i) {
        data.local_ies[i] = lighting.local_ies[static_cast<std::size_t>(i)];
    }
    data.enable_tiled_lights = lighting.enable_tiled_lights ? 1.f : 0.f;
    data.tile_grid_w = 8.f;
    data.tile_grid_h = 4.f;
    data.max_lights_per_tile = 8.f;
    data.z_slices = 4.f;
    data.z_near = 0.5f;
    data.z_far = 80.f;
    for (int i = 0; i < 128; ++i) {
        data.tile_light_count[i] = static_cast<float>(lighting.tile_light_count[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < 1024; ++i) {
        data.tile_light_index[i] = static_cast<float>(lighting.tile_light_index[static_cast<std::size_t>(i)]);
    }
    data.enable_probe_gi = lighting.enable_probe_gi ? 1.f : 0.f;
    data.probe_gi_intensity = lighting.probe_gi_intensity;
    data.probe_rgb_scale = lighting.probe_rgb_scale;
    data.probe_nx = static_cast<float>(lighting.probe_nx);
    data.probe_origin[0] = lighting.probe_origin.x;
    data.probe_origin[1] = lighting.probe_origin.y;
    data.probe_origin[2] = lighting.probe_origin.z;
    data.probe_ny = static_cast<float>(lighting.probe_ny);
    data.probe_spacing[0] = lighting.probe_spacing.x;
    data.probe_spacing[1] = lighting.probe_spacing.y;
    data.probe_spacing[2] = lighting.probe_spacing.z;
    data.probe_nz = static_cast<float>(lighting.probe_nz);
    data.enable_soft_shadow_mask = lighting.enable_soft_shadow_mask ? 1.f : 0.f;

    void* ptr = nullptr;
    if (FAILED(frame_cb_->Map(0, nullptr, &ptr))) {
        return Status::Fail("Map frame CB failed");
    }
    std::memcpy(static_cast<char*>(ptr) + FrameCbOffset(), &data, sizeof(data));
    frame_cb_->Unmap(0, nullptr);

    const Mat4& shadow_vp =
            lighting.cascade_count > 0 ? lighting.cascade_view_proj[0] : lighting.light_view_proj;
    float shadow_frame[16]{};
    std::memcpy(shadow_frame, shadow_vp.m.data(), sizeof(shadow_frame));
    if (FAILED(shadow_frame_cb_->Map(0, nullptr, &ptr))) {
        return Status::Fail("Map shadow frame CB failed");
    }
    std::memcpy(static_cast<char*>(ptr) + ShadowVpCbOffset(0), shadow_frame, sizeof(shadow_frame));
    shadow_frame_cb_->Unmap(0, nullptr);

    lighting_ = lighting;
    bound_shadow_slot_ = 0;
    return Status::Ok();
}

void D3D12Device::BindSceneColorTargets() {
    D3D12_VIEWPORT vp{};
    vp.MaxDepth = 1.f;
    if (draw_vp_on_) {
        vp.TopLeftX = draw_vp_x_;
        vp.TopLeftY = draw_vp_y_;
        vp.Width = draw_vp_w_;
        vp.Height = draw_vp_h_;
    } else {
        vp.Width = static_cast<float>(width_);
        vp.Height = static_cast<float>(height_);
    }
    command_list_->RSSetViewports(1, &vp);
    D3D12_RECT scissor{static_cast<LONG>(vp.TopLeftX), static_cast<LONG>(vp.TopLeftY),
                                         static_cast<LONG>(vp.TopLeftX + vp.Width),
                                         static_cast<LONG>(vp.TopLeftY + vp.Height)};
    command_list_->RSSetScissorRects(1, &scissor);

    // After ResolvePostEffects, scene_color is still bound as SRV in post_srv_heap for this
    // command list. Rebinding it as RT caused DEVICE_REMOVED (0x887A0005) on Present.
    // Late draws (transparent / Sandbox scale instancing) go to the LDR backbuffer instead.
    if (prefer_ldr_ || post_resolved_this_frame_ || !scene_color_ || !hdr_rtv_heap_) {
        const auto index = CurrentBbIndex();
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv{
                rtv_heap_->GetCPUDescriptorHandleForHeapStart().ptr +
                static_cast<SIZE_T>(index) * rtv_descriptor_size_};
        if (dsv_) {
            if (depth_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
                Transition(dsv_.Get(), depth_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                depth_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            }
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        } else {
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        }
        return;
    }

    if (scene_color_state_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        Transition(scene_color_.Get(), scene_color_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        scene_color_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE hdr_rtv =
            hdr_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    if (dsv_) {
        if (depth_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
            Transition(dsv_.Get(), depth_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            depth_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        command_list_->OMSetRenderTargets(1, &hdr_rtv, FALSE, &dsv);
    } else {
        command_list_->OMSetRenderTargets(1, &hdr_rtv, FALSE, nullptr);
    }
}

Status D3D12Device::DrawLitCube(const LitDrawItem& item) {
    return DrawLitCubes(std::span<const LitDrawItem>(&item, 1));
}

Status D3D12Device::DrawLitCubes(std::span<const LitDrawItem> items) {
    return DrawLitCubesWithPso(items, lit_pso_.Get());
}

Status D3D12Device::DrawTransparentLitCubes(std::span<const LitDrawItem> items) {
    if (!lit_pso_transparent_) {
        return Status::Fail("Transparent lit PSO not ready");
    }
    return DrawLitCubesWithPso(items, lit_pso_transparent_.Get());
}

Status D3D12Device::DrawLitCubesWithPso(std::span<const LitDrawItem> items, ID3D12PipelineState* pso) {
    if (!lit_ready_) {
        return Status::Fail("SetupLitMesh not called");
    }
    if (items.empty()) {
        return Status::Ok();
    }
    BindSceneColorTargets();
    if (shadow_map_ && shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        Transition(shadow_map_.Get(), shadow_map_state_,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (local_shadow_map_ &&
            local_shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        Transition(local_shadow_map_.Get(), local_shadow_map_state_,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        local_shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    command_list_->SetPipelineState(pso);
    command_list_->SetGraphicsRootSignature(lit_root_.Get());
    command_list_->SetGraphicsRootConstantBufferView(
            0, frame_cb_->GetGPUVirtualAddress() + FrameCbOffset());
    ID3D12DescriptorHeap* heaps[] = {shadow_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootDescriptorTable(
            2, shadow_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    struct ObjectData {
        float world[16];
        float color[4];
        float metallic;
        float roughness;
        float use_albedo;
        float use_orm;
        float tex_slot;
        float uv_scale;
        float use_instances;
        float pad;
        float detail_blend;
        float detail_uv_scale;
        float triplanar;
        float triplanar_sharpness;
    };

    if (auto* ib = CurrentInstanceBuf()) {
        command_list_->SetGraphicsRootShaderResourceView(3, ib->GetGPUVirtualAddress());
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        const int slot = items[i].mesh_slot;
        if (slot < 0 || slot >= kMaxMeshSlots || mesh_slots_[slot].index_count == 0) {
            continue;
        }
        command_list_->IASetVertexBuffers(0, 1, &mesh_slots_[slot].vbv);
        command_list_->IASetIndexBuffer(&mesh_slots_[slot].ibv);

        ObjectData od{};
        std::memcpy(od.world, items[i].world.m.data(), sizeof(od.world));
        od.color[0] = items[i].color.r;
        od.color[1] = items[i].color.g;
        od.color[2] = items[i].color.b;
        od.color[3] = items[i].color.a;
        od.metallic = items[i].metallic;
        od.roughness = items[i].roughness;
        od.use_albedo = items[i].use_albedo ? 1.f : 0.f;
        od.use_orm = items[i].use_orm ? 1.f : 0.f;
        od.tex_slot = static_cast<float>(items[i].tex_slot);
        od.uv_scale = items[i].uv_scale > 0.f ? items[i].uv_scale : 1.f;
        od.use_instances = 0.f;
        // Default classic (pad=-1). Feature bindless_hot_path + capable + !headless →
        // ResourceDescriptorHeap[tex_slot?4:1]. Hardcoded pad=1 previously drifted golden.
        od.pad = BindlessAlbedoHeapPad(od.tex_slot);
        od.detail_blend = items[i].detail_blend;
        od.detail_uv_scale = items[i].detail_uv_scale > 0.f ? items[i].detail_uv_scale : 4.f;
        od.triplanar = items[i].triplanar;
        od.triplanar_sharpness =
                items[i].triplanar_sharpness > 0.f ? items[i].triplanar_sharpness : 4.f;

        const auto offset = ObjectCbOffset(i);
        void* ptr = nullptr;
        if (FAILED(object_cb_->Map(0, nullptr, &ptr))) {
            return Status::Fail("Map object CB failed");
        }
        std::memcpy(static_cast<char*>(ptr) + offset, &od, sizeof(od));
        object_cb_->Unmap(0, nullptr);

        command_list_->SetGraphicsRootConstantBufferView(
                1, object_cb_->GetGPUVirtualAddress() + offset);
        command_list_->DrawIndexedInstanced(mesh_slots_[slot].index_count, 1, 0, 0, 0);
    }
    lit_draws_ += static_cast<std::uint32_t>(items.size());
    return Status::Ok();
}

Status D3D12Device::UploadInstanceTransforms(std::span<const Mat4> worlds) {
    instance_worlds_.assign(worlds.begin(), worlds.end());
    if (worlds.empty() || !device_) {
        return Status::Ok();
    }
    const UINT64 bytes = static_cast<UINT64>(worlds.size() * sizeof(Mat4));
    auto& buf = instance_bufs_[frame_index_];
    auto& buf_bytes = instance_buf_bytes_[frame_index_];
    // This frame's allocator is idle (BeginFrame waited) — safe to recreate this slot.
    if (!buf || buf_bytes < bytes) {
        buf.Reset();
        D3D12_HEAP_PROPERTIES upload{};
        upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = (std::max)(bytes, static_cast<UINT64>(1024 * sizeof(Mat4)));
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr =
                device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &desc,
                                                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                 IID_PPV_ARGS(&buf));
        if (FAILED(hr)) {
            return Status::Fail("Create instance buffer failed: " + HrToString(hr));
        }
        buf_bytes = desc.Width;
    }
    void* ptr = nullptr;
    if (FAILED(buf->Map(0, nullptr, &ptr))) {
        return Status::Fail("Map instance buffer failed");
    }
    std::memcpy(ptr, worlds.data(), static_cast<std::size_t>(bytes));
    buf->Unmap(0, nullptr);
    engine::SetFeatureOverride("gpu_instancing", true);
    return Status::Ok();
}

ID3D12Resource* D3D12Device::CurrentInstanceBuf() const {
    return instance_bufs_[frame_index_].Get();
}

Status D3D12Device::DrawLitInstanced(const LitDrawItem& prototype, std::uint32_t instance_count) {
    if (!lit_ready_) {
        return Status::Fail("SetupLitMesh not called");
    }
    if (instance_count == 0) {
        return Status::Ok();
    }
    auto* ib = CurrentInstanceBuf();
    if (!ib || instance_worlds_.size() < instance_count) {
        return IDevice::DrawLitInstanced(prototype, instance_count);
    }
    BindSceneColorTargets();
    if (shadow_map_ && shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        Transition(shadow_map_.Get(), shadow_map_state_,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (local_shadow_map_ &&
            local_shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        Transition(local_shadow_map_.Get(), local_shadow_map_state_,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        local_shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    command_list_->SetPipelineState(lit_pso_.Get());
    command_list_->SetGraphicsRootSignature(lit_root_.Get());
    command_list_->SetGraphicsRootConstantBufferView(
            0, frame_cb_->GetGPUVirtualAddress() + FrameCbOffset());
    ID3D12DescriptorHeap* heaps[] = {shadow_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootDescriptorTable(
            2, shadow_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->SetGraphicsRootShaderResourceView(3, ib->GetGPUVirtualAddress());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const int slot = prototype.mesh_slot;
    if (slot < 0 || slot >= kMaxMeshSlots || mesh_slots_[slot].index_count == 0) {
        return Status::Fail("Invalid mesh for instancing");
    }
    command_list_->IASetVertexBuffers(0, 1, &mesh_slots_[slot].vbv);
    command_list_->IASetIndexBuffer(&mesh_slots_[slot].ibv);

    struct ObjectData {
        float world[16];
        float color[4];
        float metallic;
        float roughness;
        float use_albedo;
        float use_orm;
        float tex_slot;
        float uv_scale;
        float use_instances;
        float pad;
        float detail_blend;
        float detail_uv_scale;
        float triplanar;
        float triplanar_sharpness;
    } od{};
    std::memcpy(od.world, Mat4::Identity().m.data(), sizeof(od.world));
    od.color[0] = prototype.color.r;
    od.color[1] = prototype.color.g;
    od.color[2] = prototype.color.b;
    od.color[3] = prototype.color.a;
    od.metallic = prototype.metallic;
    od.roughness = prototype.roughness;
    od.use_albedo = prototype.use_albedo ? 1.f : 0.f;
    od.use_orm = prototype.use_orm ? 1.f : 0.f;
    od.tex_slot = static_cast<float>(prototype.tex_slot);
    od.uv_scale = prototype.uv_scale > 0.f ? prototype.uv_scale : 1.f;
    od.use_instances = 1.f;
    // Instanced path stays classic; bindless_hot_path is opaque DrawLit only.
    od.pad = -1.f;
    od.detail_blend = prototype.detail_blend;
    od.detail_uv_scale = prototype.detail_uv_scale > 0.f ? prototype.detail_uv_scale : 4.f;
    od.triplanar = prototype.triplanar;
    od.triplanar_sharpness =
            prototype.triplanar_sharpness > 0.f ? prototype.triplanar_sharpness : 4.f;

    const auto offset = ObjectCbOffset(kMaxLitDraws - 1);  // dedicated late/instanced slot
    void* ptr = nullptr;
    if (FAILED(object_cb_->Map(0, nullptr, &ptr))) {
        return Status::Fail("Map object CB failed");
    }
    std::memcpy(static_cast<char*>(ptr) + offset, &od, sizeof(od));
    object_cb_->Unmap(0, nullptr);
    command_list_->SetGraphicsRootConstantBufferView(
            1, object_cb_->GetGPUVirtualAddress() + offset);
    // Prefer CPU instance count. ExecuteIndirect + Cull CS can disagree with the
    // uploaded transform buffer and shear the green scale pillars on D3D12.
    command_list_->DrawIndexedInstanced(mesh_slots_[slot].index_count, instance_count, 0, 0, 0);
    lit_draws_ += instance_count;
    return Status::Ok();
}

UINT64 D3D12Device::FrameCbOffset() const { return static_cast<UINT64>(frame_index_) * kFrameCbBytes; }

Status D3D12Device::CreateLitAlbedoTexture() {
    constexpr UINT w = 64;
    constexpr UINT h = 64;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4));
    for (UINT y = 0; y < h; ++y) {
        for (UINT x = 0; x < w; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            const std::uint8_t c = light ? 240 : 48;
            const auto i = static_cast<std::size_t>((y * w + x) * 4);
            pixels[i + 0] = c;
            pixels[i + 1] = c;
            pixels[i + 2] = c;
            pixels[i + 3] = 255;
        }
    }
    return UploadRgbaTexture(lit_albedo_, 1, pixels.data(), static_cast<int>(w),
                                                     static_cast<int>(h));
}

Status D3D12Device::CreateLitOrmTexture() {
    // Neutral ORM: AO=1, roughness=0.5, metallic=0
    constexpr UINT w = 4;
    constexpr UINT h = 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = 255;
        pixels[i + 1] = 128;
        pixels[i + 2] = 0;
        pixels[i + 3] = 255;
    }
    return UploadRgbaTexture(lit_orm_, 3, pixels.data(), static_cast<int>(w), static_cast<int>(h));
}

Status D3D12Device::CreateLitAlbedoTextureSlot1() {
    constexpr UINT w = 4;
    constexpr UINT h = 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4), 255);
    return UploadRgbaTexture(lit_albedo2_, 4, pixels.data(), static_cast<int>(w),
                                                     static_cast<int>(h));
}

Status D3D12Device::CreateLitOrmTextureSlot1() {
    constexpr UINT w = 4;
    constexpr UINT h = 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = 255;
        pixels[i + 1] = 128;
        pixels[i + 2] = 0;
        pixels[i + 3] = 255;
    }
    return UploadRgbaTexture(lit_orm2_, 5, pixels.data(), static_cast<int>(w), static_cast<int>(h));
}

Status D3D12Device::UploadLitAlbedoRgba(const std::uint8_t* rgba, int width, int height, int slot) {
    if (!lit_ready_) {
        return Status::Fail("SetupLitMesh not called");
    }
    if (slot == 0) {
        return UploadRgbaTexture(lit_albedo_, 1, rgba, width, height);
    }
    if (slot == 1) {
        return UploadRgbaTexture(lit_albedo2_, 4, rgba, width, height);
    }
    return Status::Fail("Invalid albedo slot");
}

Status D3D12Device::UploadLitOrmRgba(const std::uint8_t* rgba, int width, int height, int slot) {
    if (!lit_ready_) {
        return Status::Fail("SetupLitMesh not called");
    }
    if (slot == 0) {
        return UploadRgbaTexture(lit_orm_, 3, rgba, width, height);
    }
    if (slot == 1) {
        return UploadRgbaTexture(lit_orm2_, 5, rgba, width, height);
    }
    return Status::Fail("Invalid ORM slot");
}

Status D3D12Device::CreateCubeMesh() {
    const LitVertex verts[] = {
            // +Z
            {-0.5f, -0.5f, 0.5f, 0, 0, 1, 0, 0},  {0.5f, -0.5f, 0.5f, 0, 0, 1, 1, 0},
            {0.5f, 0.5f, 0.5f, 0, 0, 1, 1, 1},    {-0.5f, 0.5f, 0.5f, 0, 0, 1, 0, 1},
            // -Z
            {0.5f, -0.5f, -0.5f, 0, 0, -1, 0, 0}, {-0.5f, -0.5f, -0.5f, 0, 0, -1, 1, 0},
            {-0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 1}, {0.5f, 0.5f, -0.5f, 0, 0, -1, 0, 1},
            // +X
            {0.5f, -0.5f, 0.5f, 1, 0, 0, 0, 0},   {0.5f, -0.5f, -0.5f, 1, 0, 0, 1, 0},
            {0.5f, 0.5f, -0.5f, 1, 0, 0, 1, 1},   {0.5f, 0.5f, 0.5f, 1, 0, 0, 0, 1},
            // -X
            {-0.5f, -0.5f, -0.5f, -1, 0, 0, 0, 0},{-0.5f, -0.5f, 0.5f, -1, 0, 0, 1, 0},
            {-0.5f, 0.5f, 0.5f, -1, 0, 0, 1, 1},  {-0.5f, 0.5f, -0.5f, -1, 0, 0, 0, 1},
            // +Y
            {-0.5f, 0.5f, 0.5f, 0, 1, 0, 0, 0},   {0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 0},
            {0.5f, 0.5f, -0.5f, 0, 1, 0, 1, 1},   {-0.5f, 0.5f, -0.5f, 0, 1, 0, 0, 1},
            // -Y
            {-0.5f, -0.5f, -0.5f, 0, -1, 0, 0, 0},{0.5f, -0.5f, -0.5f, 0, -1, 0, 1, 0},
            {0.5f, -0.5f, 0.5f, 0, -1, 0, 1, 1},  {-0.5f, -0.5f, 0.5f, 0, -1, 0, 0, 1},
    };
    const std::uint32_t indices[] = {
            0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
            12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };
    return UploadLitGeometry(0, std::span<const LitVertex>(verts, 24),
                                                     std::span<const std::uint32_t>(indices, 36));
}

Status D3D12Device::UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                                                 std::span<const std::uint32_t> indices) {
    if (mesh_slot < 0 || mesh_slot >= kMaxMeshSlots) {
        return Status::Fail("Invalid mesh slot");
    }
    if (vertices.empty() || indices.empty()) {
        return Status::Fail("Empty lit geometry");
    }
    if (!device_) {
        return Status::Fail("Device not ready");
    }

    MeshSlotGpu& slot = mesh_slots_[mesh_slot];
    // Previous frames may still reference these upload buffers on the GPU.
    if (slot.vb || slot.ib) {
        WaitGpuSubmitted();
    }

    auto create_upload = [&](const void* data, UINT size, ComPtr<ID3D12Resource>& out) -> Status {
        out.Reset();
        D3D12_HEAP_PROPERTIES upload{};
        upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buf{};
        buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf.Width = size;
        buf.Height = 1;
        buf.DepthOrArraySize = 1;
        buf.MipLevels = 1;
        buf.SampleDesc.Count = 1;
        buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr = device_->CreateCommittedResource(
                &upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&out));
        if (FAILED(hr)) {
            return Status::Fail("Create lit mesh buffer failed");
        }
        void* mapped = nullptr;
        out->Map(0, nullptr, &mapped);
        std::memcpy(mapped, data, size);
        out->Unmap(0, nullptr);
        return Status::Ok();
    };

    const UINT vb_size = static_cast<UINT>(vertices.size() * sizeof(LitVertex));
    const UINT ib_size = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    if (auto st = create_upload(vertices.data(), vb_size, slot.vb); !st) {
        return st;
    }
    if (auto st = create_upload(indices.data(), ib_size, slot.ib); !st) {
        return st;
    }
    slot.vbv.BufferLocation = slot.vb->GetGPUVirtualAddress();
    slot.vbv.SizeInBytes = vb_size;
    slot.vbv.StrideInBytes = sizeof(LitVertex);
    slot.ibv.BufferLocation = slot.ib->GetGPUVirtualAddress();
    slot.ibv.SizeInBytes = ib_size;
    slot.ibv.Format = DXGI_FORMAT_R32_UINT;
    slot.index_count = static_cast<UINT>(indices.size());
    return Status::Ok();
}

Status D3D12Device::CreateLitConstantBuffers() {
    auto make_cb = [&](UINT64 size, ComPtr<ID3D12Resource>& out) -> Status {
        D3D12_HEAP_PROPERTIES upload{};
        upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buf{};
        buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf.Width = size;
        buf.Height = 1;
        buf.DepthOrArraySize = 1;
        buf.MipLevels = 1;
        buf.SampleDesc.Count = 1;
        buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr = device_->CreateCommittedResource(
                &upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&out));
        if (FAILED(hr)) {
            return Status::Fail("Create CB failed");
        }
        return Status::Ok();
    };
    if (auto st = make_cb(kFrameCbBytes * kFrameCount, frame_cb_); !st) {
        return st;
    }
    if (auto st = make_cb(256ull * kShadowVpSlots * kFrameCount, shadow_frame_cb_); !st) {
        return st;
    }
    if (auto st = make_cb(256ull * kMaxLitDraws * kFrameCount, object_cb_); !st) {
        return st;
    }
    return Status::Ok();
}

}  // namespace engine::rhi
