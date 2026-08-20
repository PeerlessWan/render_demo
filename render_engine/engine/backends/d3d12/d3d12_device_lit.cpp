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

Status D3D12Device::BeginShadowPass() {
  if (!lit_ready_ || !shadow_map_) {
    return Status::Fail("SetupLitMesh not called");
  }
  if (shadow_map_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
    Transition(shadow_map_.Get(), shadow_map_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    shadow_map_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE shadow_dsv =
      shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
  command_list_->ClearDepthStencilView(shadow_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  command_list_->OMSetRenderTargets(0, nullptr, FALSE, &shadow_dsv);

  D3D12_VIEWPORT vp{};
  vp.Width = static_cast<float>(kShadowMapSize);
  vp.Height = static_cast<float>(kShadowMapSize);
  vp.MaxDepth = 1.f;
  command_list_->RSSetViewports(1, &vp);
  D3D12_RECT scissor{0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize)};
  command_list_->RSSetScissorRects(1, &scissor);
  shadow_active_ = true;
  local_shadow_active_ = false;
  bound_shadow_slot_ = 0;
  shadow_draws_ = 0;
  return Status::Ok();
}

Status D3D12Device::BindShadowCascade(int cascade_index) {
  if (!lit_ready_ || !shadow_active_) {
    return Status::Fail("BeginShadowPass not active");
  }
  if (cascade_index < 0 || cascade_index >= lighting_.cascade_count) {
    return Status::Fail(ErrorCode::InvalidArgument, "Invalid cascade index");
  }

  float shadow_frame[16]{};
  std::memcpy(shadow_frame,
              lighting_.cascade_view_proj[static_cast<std::size_t>(cascade_index)].m.data(),
              sizeof(shadow_frame));
  void* ptr = nullptr;
  if (FAILED(shadow_frame_cb_->Map(0, nullptr, &ptr))) {
    return Status::Fail("Map shadow frame CB failed");
  }
  // Per-cascade slot: must not overwrite other cascades before GPU executes.
  std::memcpy(static_cast<char*>(ptr) + ShadowVpCbOffset(cascade_index), shadow_frame,
              sizeof(shadow_frame));
  shadow_frame_cb_->Unmap(0, nullptr);

  const int tiles_per_row = (std::max)(1, lighting_.cascade_tiles_per_row);
  const float tile = static_cast<float>(kShadowMapSize) / static_cast<float>(tiles_per_row);
  const int ix = cascade_index % tiles_per_row;
  const int iy = cascade_index / tiles_per_row;

  D3D12_VIEWPORT vp{};
  vp.TopLeftX = static_cast<float>(ix) * tile;
  vp.TopLeftY = static_cast<float>(iy) * tile;
  vp.Width = tile;
  vp.Height = tile;
  vp.MaxDepth = 1.f;
  command_list_->RSSetViewports(1, &vp);

  D3D12_RECT scissor{};
  scissor.left = static_cast<LONG>(vp.TopLeftX);
  scissor.top = static_cast<LONG>(vp.TopLeftY);
  scissor.right = scissor.left + static_cast<LONG>(tile);
  scissor.bottom = scissor.top + static_cast<LONG>(tile);
  command_list_->RSSetScissorRects(1, &scissor);

  bound_shadow_slot_ = cascade_index;
  return Status::Ok();
}

Status D3D12Device::DrawShadowCubes(std::span<const LitDrawItem> items) {
  if (!lit_ready_ || (!shadow_active_ && !local_shadow_active_)) {
    return Status::Fail("BeginShadowPass/BeginLocalShadowPass not active");
  }
  if (items.empty()) {
    return Status::Ok();
  }

  command_list_->SetPipelineState(shadow_pso_.Get());
  command_list_->SetGraphicsRootSignature(shadow_root_.Get());
  command_list_->SetGraphicsRootConstantBufferView(
      0, shadow_frame_cb_->GetGPUVirtualAddress() + ShadowVpCbOffset(bound_shadow_slot_));
  command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  for (std::size_t i = 0; i < items.size(); ++i) {
    const int slot = items[i].mesh_slot;
    if (slot < 0 || slot >= kMaxMeshSlots || mesh_slots_[slot].index_count == 0) {
      continue;
    }
    command_list_->IASetVertexBuffers(0, 1, &mesh_slots_[slot].vbv);
    command_list_->IASetIndexBuffer(&mesh_slots_[slot].ibv);

    float world[16]{};
    std::memcpy(world, items[i].world.m.data(), sizeof(world));
    const auto offset = ObjectCbOffset(shadow_draws_);
    void* ptr = nullptr;
    if (FAILED(object_cb_->Map(0, nullptr, &ptr))) {
      return Status::Fail("Map object CB failed");
    }
    std::memcpy(static_cast<char*>(ptr) + offset, world, sizeof(world));
    object_cb_->Unmap(0, nullptr);

    command_list_->SetGraphicsRootConstantBufferView(
        1, object_cb_->GetGPUVirtualAddress() + offset);
    command_list_->DrawIndexedInstanced(mesh_slots_[slot].index_count, 1, 0, 0, 0);
    ++shadow_draws_;
  }
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

Status D3D12Device::EndShadowPass() {
  if (!shadow_active_) {
    return Status::Fail("BeginShadowPass not active");
  }
  Transition(shadow_map_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  BindSceneColorTargets();
  shadow_active_ = false;
  return Status::Ok();
}

Status D3D12Device::BeginLocalShadowPass() {
  if (!lit_ready_ || !local_shadow_map_) {
    return Status::Fail("SetupLitMesh not called");
  }
  if (local_shadow_map_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
    Transition(local_shadow_map_.Get(), local_shadow_map_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    local_shadow_map_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE local_dsv =
      local_shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
  // Clear whole atlas; BindLocalShadowTile sets per-tile viewport.
  command_list_->ClearDepthStencilView(local_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  command_list_->OMSetRenderTargets(0, nullptr, FALSE, &local_dsv);

  local_shadow_active_ = true;
  shadow_active_ = false;
  bound_shadow_slot_ = 4;
  // Default to tile 0 for callers that skip BindLocalShadowTile.
  return BindLocalShadowTile(0);
}

Status D3D12Device::BindLocalShadowTile(int tile_index) {
  if (!lit_ready_ || !local_shadow_active_) {
    return Status::Fail("BeginLocalShadowPass not active");
  }
  const int count = (std::max)(1, lighting_.local_shadow_tile_count > 0
                                      ? lighting_.local_shadow_tile_count
                                      : lighting_.local_shadow_count);
  if (tile_index < 0 || tile_index >= count) {
    return Status::Fail(ErrorCode::InvalidArgument, "Invalid local shadow tile index");
  }

  float shadow_frame[16]{};
  std::memcpy(shadow_frame,
              lighting_.local_shadow_vps[static_cast<std::size_t>(tile_index)].m.data(),
              sizeof(shadow_frame));
  if (tile_index == 0) {
    // Compat: scheduler may still write only local_shadow_vp.
    std::memcpy(shadow_frame, lighting_.local_shadow_vp.m.data(), sizeof(shadow_frame));
  }
  const int vp_slot = 4 + tile_index;  // after CSM cascade slots 0..3
  void* ptr = nullptr;
  if (FAILED(shadow_frame_cb_->Map(0, nullptr, &ptr))) {
    return Status::Fail("Map shadow frame CB failed");
  }
  std::memcpy(static_cast<char*>(ptr) + ShadowVpCbOffset(vp_slot), shadow_frame,
              sizeof(shadow_frame));
  shadow_frame_cb_->Unmap(0, nullptr);

  const int tiles_per_row = (std::max)(1, lighting_.local_shadow_tiles_per_row);
  const float tile = static_cast<float>(kLocalShadowMapSize) / static_cast<float>(tiles_per_row);
  const int ix = tile_index % tiles_per_row;
  const int iy = tile_index / tiles_per_row;

  D3D12_VIEWPORT vp{};
  vp.TopLeftX = static_cast<float>(ix) * tile;
  vp.TopLeftY = static_cast<float>(iy) * tile;
  vp.Width = tile;
  vp.Height = tile;
  vp.MaxDepth = 1.f;
  command_list_->RSSetViewports(1, &vp);

  D3D12_RECT scissor{};
  scissor.left = static_cast<LONG>(vp.TopLeftX);
  scissor.top = static_cast<LONG>(vp.TopLeftY);
  scissor.right = scissor.left + static_cast<LONG>(tile);
  scissor.bottom = scissor.top + static_cast<LONG>(tile);
  command_list_->RSSetScissorRects(1, &scissor);
  bound_shadow_slot_ = vp_slot;
  return Status::Ok();
}

Status D3D12Device::EndLocalShadowPass() {
  if (!local_shadow_active_) {
    return Status::Fail("BeginLocalShadowPass not active");
  }
  Transition(local_shadow_map_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  local_shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  BindSceneColorTargets();
  local_shadow_active_ = false;
  return Status::Ok();
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

Status D3D12Device::EnsureProbeFaceTargets(int face_size) {
  face_size = (std::max)(8, face_size);
  if (probe_face_size_ == face_size && probe_face_color_ && probe_face_depth_) {
    return Status::Ok();
  }
  const bool resizing = probe_face_color_ != nullptr;
  if (resizing) {
    WaitGpu();
  }
  probe_face_color_.Reset();
  probe_face_depth_.Reset();
  probe_rtv_heap_.Reset();
  probe_dsv_heap_.Reset();
  probe_face_size_ = face_size;

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap{};
  rtv_heap.NumDescriptors = 1;
  rtv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  HRESULT hr = device_->CreateDescriptorHeap(&rtv_heap, IID_PPV_ARGS(&probe_rtv_heap_));
  if (FAILED(hr)) {
    return Status::Fail("probe RTV heap failed");
  }
  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap{};
  dsv_heap.NumDescriptors = 1;
  dsv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  hr = device_->CreateDescriptorHeap(&dsv_heap, IID_PPV_ARGS(&probe_dsv_heap_));
  if (FAILED(hr)) {
    return Status::Fail("probe DSV heap failed");
  }

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC color{};
  color.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  color.Width = static_cast<UINT64>(face_size);
  color.Height = static_cast<UINT>(face_size);
  color.DepthOrArraySize = 1;
  color.MipLevels = 1;
  color.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  color.SampleDesc.Count = 1;
  color.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE clear{};
  clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  clear.Color[0] = 0.05f;
  clear.Color[1] = 0.06f;
  clear.Color[2] = 0.08f;
  clear.Color[3] = 1.f;
  hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &color,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                        IID_PPV_ARGS(&probe_face_color_));
  if (FAILED(hr)) {
    return Status::Fail("probe color RT failed");
  }
  device_->CreateRenderTargetView(probe_face_color_.Get(), nullptr,
                                  probe_rtv_heap_->GetCPUDescriptorHandleForHeapStart());

  D3D12_RESOURCE_DESC depth = color;
  depth.Format = DXGI_FORMAT_D32_FLOAT;
  depth.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE dclear{};
  dclear.Format = DXGI_FORMAT_D32_FLOAT;
  dclear.DepthStencil.Depth = 1.f;
  hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth,
                                        D3D12_RESOURCE_STATE_DEPTH_WRITE, &dclear,
                                        IID_PPV_ARGS(&probe_face_depth_));
  if (FAILED(hr)) {
    return Status::Fail("probe depth RT failed");
  }
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = DXGI_FORMAT_D32_FLOAT;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  device_->CreateDepthStencilView(probe_face_depth_.Get(), &dsv,
                                  probe_dsv_heap_->GetCPUDescriptorHandleForHeapStart());
  return Status::Ok();
}

Mat4 D3D12Device::MakeProbeFaceVp(const Vec3& probe_pos, int face) {
  Vec3 forward{};
  Vec3 up{0.f, -1.f, 0.f};
  switch (face) {
    case 0:
      forward = {1.f, 0.f, 0.f};
      break;
    case 1:
      forward = {-1.f, 0.f, 0.f};
      break;
    case 2:
      forward = {0.f, 1.f, 0.f};
      up = {0.f, 0.f, 1.f};
      break;
    case 3:
      forward = {0.f, -1.f, 0.f};
      up = {0.f, 0.f, -1.f};
      break;
    case 4:
      forward = {0.f, 0.f, 1.f};
      break;
    default:
      forward = {0.f, 0.f, -1.f};
      break;
  }
  return Mat4::Perspective(1.57079632679f, 1.f, 0.05f, 80.f) *
         Mat4::LookAt(probe_pos, probe_pos + forward, up);
}

Status D3D12Device::CaptureReflectionProbeGpu(const Vec3& probe_pos, int face_size,
                                 std::span<const LitDrawItem> items) {
  if (!lit_ready_) {
    return Status::Fail("SetupLitMesh not called");
  }
  if (auto st = EnsureProbeFaceTargets(face_size); !st) {
    return st;
  }
  if (!reflection_cube_ || probe_cube_size_ != probe_face_size_) {
    // Create/replace cubemap without WaitGpu mid-frame (safe if first use this frame).
    reflection_cube_.Reset();
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(probe_face_size_);
    desc.Height = static_cast<UINT>(probe_face_size_);
    desc.DepthOrArraySize = 6;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const HRESULT hr = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&reflection_cube_));
    if (FAILED(hr)) {
      return Status::Fail("Create probe cubemap failed");
    }
    probe_cube_size_ = probe_face_size_;
    reflection_cube_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  }

  FrameLighting saved = lighting_;
  for (int face = 0; face < 6; ++face) {
    FrameLighting face_lighting = saved;
    face_lighting.view_proj = MakeProbeFaceVp(probe_pos, face);
    face_lighting.prev_view_proj = face_lighting.view_proj;
    face_lighting.eye = probe_pos;
    face_lighting.enable_reflection_probe = false;
    face_lighting.enable_ibl = false;
    face_lighting.enable_taa = false;
    face_lighting.enable_ssao = false;
    if (auto st = SetFrameLighting(face_lighting); !st) {
      return st;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = probe_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = probe_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    const float clear_color[] = {0.05f, 0.06f, 0.08f, 1.f};
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    command_list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    command_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(probe_face_size_);
    vp.Height = static_cast<float>(probe_face_size_);
    vp.MaxDepth = 1.f;
    D3D12_RECT sc{0, 0, probe_face_size_, probe_face_size_};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sc);

    // Draw into probe face without rebinding scene color.
    command_list_->SetPipelineState(lit_pso_.Get());
    command_list_->SetGraphicsRootSignature(lit_root_.Get());
    command_list_->SetGraphicsRootConstantBufferView(
        0, frame_cb_->GetGPUVirtualAddress() + FrameCbOffset());
    ID3D12DescriptorHeap* heaps[] = {shadow_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootDescriptorTable(
        2, shadow_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    if (auto* ib = CurrentInstanceBuf()) {
      command_list_->SetGraphicsRootShaderResourceView(3, ib->GetGPUVirtualAddress());
    }
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
    };
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
      od.pad = -1.f;  // probe faces: classic only (no bindless_hot_path)
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

    Transition(probe_face_color_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (reflection_cube_state_ != D3D12_RESOURCE_STATE_COPY_DEST) {
      Transition(reflection_cube_.Get(), reflection_cube_state_,
                 D3D12_RESOURCE_STATE_COPY_DEST);
      reflection_cube_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = probe_face_color_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = reflection_cube_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = static_cast<UINT>(face);
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(probe_face_color_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_RENDER_TARGET);
  }
  if (reflection_cube_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
    Transition(reflection_cube_.Get(), reflection_cube_state_,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    reflection_cube_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  }
  if (auto st = BindCubeSrv(10, reflection_cube_.Get()); !st) {
    return st;
  }
  if (auto st = SetFrameLighting(saved); !st) {
    return st;
  }
  BindSceneColorTargets();
  return Status::Ok();
}

UINT64 D3D12Device::FrameCbOffset() const { return static_cast<UINT64>(frame_index_) * kFrameCbBytes; }

UINT64 D3D12Device::SkyCbOffset() const { return static_cast<UINT64>(frame_index_) * 256ull; }

UINT64 D3D12Device::ShadowVpCbOffset(int slot) const {
  const int s = (std::max)(0, (std::min)(slot, static_cast<int>(kShadowVpSlots) - 1));
  return (static_cast<UINT64>(frame_index_) * kShadowVpSlots + static_cast<UINT64>(s)) * 256ull;
}

Status D3D12Device::CreateShadowMap() {
  shadow_map_.Reset();
  shadow_dsv_heap_.Reset();
  shadow_srv_heap_.Reset();

  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap{};
  dsv_heap.NumDescriptors = 1;
  dsv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  HRESULT hr = device_->CreateDescriptorHeap(&dsv_heap, IID_PPV_ARGS(&shadow_dsv_heap_));
  if (FAILED(hr)) {
    return Status::Fail("Create shadow DSV heap failed");
  }

  D3D12_DESCRIPTOR_HEAP_DESC srv_heap{};
  srv_heap.NumDescriptors = 16;  // t0..t8 + t10..t12 + bindless-friendly room
  srv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srv_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  hr = device_->CreateDescriptorHeap(&srv_heap, IID_PPV_ARGS(&shadow_srv_heap_));
  if (FAILED(hr)) {
    return Status::Fail("Create shadow SRV heap failed");
  }
  cbv_srv_uav_descriptor_size_ =
      device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = kShadowMapSize;
  desc.Height = kShadowMapSize;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE clear{};
  clear.Format = DXGI_FORMAT_D32_FLOAT;
  clear.DepthStencil.Depth = 1.0f;

  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
  hr = device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                        IID_PPV_ARGS(&shadow_map_));
  if (FAILED(hr)) {
    return Status::Fail("Create shadow map failed: " + HrToString(hr));
  }

  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = DXGI_FORMAT_D32_FLOAT;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  device_->CreateDepthStencilView(shadow_map_.Get(), &dsv,
                                  shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart());

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  device_->CreateShaderResourceView(shadow_map_.Get(), &srv,
                                    shadow_srv_heap_->GetCPUDescriptorHandleForHeapStart());

  shadow_map_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  lit_albedo_.Reset();
  lit_orm_.Reset();
  lit_albedo2_.Reset();
  lit_orm2_.Reset();
  local_shadow_map_.Reset();
  local_shadow_dsv_heap_.Reset();
  return Status::Ok();
}

Status D3D12Device::CreateLocalShadowMap() {
  local_shadow_map_.Reset();
  local_shadow_dsv_heap_.Reset();

  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap{};
  dsv_heap.NumDescriptors = 1;
  dsv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  HRESULT hr = device_->CreateDescriptorHeap(&dsv_heap, IID_PPV_ARGS(&local_shadow_dsv_heap_));
  if (FAILED(hr)) {
    return Status::Fail("Create local shadow DSV heap failed");
  }

  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = kLocalShadowMapSize;
  desc.Height = kLocalShadowMapSize;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE clear{};
  clear.Format = DXGI_FORMAT_D32_FLOAT;
  clear.DepthStencil.Depth = 1.0f;

  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
  hr = device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                        IID_PPV_ARGS(&local_shadow_map_));
  if (FAILED(hr)) {
    return Status::Fail("Create local shadow map failed: " + HrToString(hr));
  }

  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = DXGI_FORMAT_D32_FLOAT;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  device_->CreateDepthStencilView(local_shadow_map_.Get(), &dsv,
                                  local_shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart());

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  D3D12_CPU_DESCRIPTOR_HANDLE local_srv = shadow_srv_heap_->GetCPUDescriptorHandleForHeapStart();
  local_srv.ptr += static_cast<SIZE_T>(2) * cbv_srv_uav_descriptor_size_;
  device_->CreateShaderResourceView(local_shadow_map_.Get(), &srv, local_srv);

  local_shadow_map_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  return Status::Ok();
}

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

Status D3D12Device::BindReflectionCubeSrv() {
  return BindCubeSrv(10, reflection_cube_.Get());
}

Status D3D12Device::EnsureDefaultReflectionCubemap() {
  constexpr int kFace = 8;
  std::vector<std::uint8_t> faces(static_cast<std::size_t>(6 * kFace * kFace * 4));
  for (int face = 0; face < 6; ++face) {
    for (int y = 0; y < kFace; ++y) {
      for (int x = 0; x < kFace; ++x) {
        const std::size_t i =
            static_cast<std::size_t>(((face * kFace + y) * kFace + x) * 4);
        const float t = static_cast<float>(y) / static_cast<float>(kFace);
        faces[i + 0] = static_cast<std::uint8_t>(40 + 80 * t);
        faces[i + 1] = static_cast<std::uint8_t>(50 + 90 * t);
        faces[i + 2] = static_cast<std::uint8_t>(70 + 110 * t);
        faces[i + 3] = 255;
      }
    }
  }
  if (auto st = UploadReflectionCubemap(faces.data(), kFace); !st) {
    return st;
  }
  if (auto st = UploadIblPrefilterCubemap(faces.data(), kFace); !st) {
    return st;
  }
  if (auto st = UploadIblIrradianceCubemap(faces.data(), kFace); !st) {
    return st;
  }
  std::vector<std::uint8_t> lut(128 * 128 * 4);
  for (std::size_t i = 0; i < lut.size(); i += 4) {
    lut[i + 0] = 200;
    lut[i + 1] = 40;
    lut[i + 2] = 0;
    lut[i + 3] = 255;
  }
  return UploadIblBrdfLut(lut.data(), 128, 128);
}

Status D3D12Device::UploadReflectionCubemap(const std::uint8_t* rgba_faces, int face_size) {
  if (auto st = UploadCubemapResource(reflection_cube_, rgba_faces, face_size); !st) {
    return st;
  }
  reflection_cube_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  probe_cube_size_ = face_size;
  return BindCubeSrv(10, reflection_cube_.Get());
}

Status D3D12Device::BindCubeSrv(UINT slot, ID3D12Resource* cube) {
  if (!shadow_srv_heap_ || !cube) {
    return Status::Fail("Cube SRV bind missing");
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.TextureCube.MipLevels = 1;
  D3D12_CPU_DESCRIPTOR_HANDLE handle = shadow_srv_heap_->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(slot) * cbv_srv_uav_descriptor_size_;
  device_->CreateShaderResourceView(cube, &srv, handle);
  return Status::Ok();
}

Status D3D12Device::UploadIblIrradianceCubemap(const std::uint8_t* rgba_faces, int face_size) {
  // Dedicated irradiance cube at t7.
  if (auto st = UploadCubemapResource(ibl_irradiance_, rgba_faces, face_size); !st) {
    return st;
  }
  return BindCubeSrv(7, ibl_irradiance_.Get());
}

Status D3D12Device::UploadIblPrefilterCubemap(const std::uint8_t* rgba_faces, int face_size) {
  // Dedicated specular prefilter at t6 (independent of reflection probe t10).
  if (auto st = UploadCubemapResource(ibl_prefilter_, rgba_faces, face_size); !st) {
    return st;
  }
  return BindCubeSrv(6, ibl_prefilter_.Get());
}

Status D3D12Device::UploadIblBrdfLut(const std::uint8_t* rgba, int w, int h) {
  if (!rgba || w <= 0 || h <= 0) {
    return Status::Fail("Invalid BRDF LUT");
  }
  return UploadRgbaTexture(ibl_brdf_lut_, 8, rgba, w, h);
}

Status D3D12Device::EnsureDefaultProbeGiAndSoftShadowTextures() {
  const std::uint8_t black[4] = {0, 0, 0, 255};
  const std::uint8_t white[4] = {255, 255, 255, 255};
  if (!probe_gi_atlas_) {
    if (auto st = UploadRgbaTexture(probe_gi_atlas_, 11, black, 1, 1); !st) {
      return st;
    }
  }
  if (!soft_shadow_mask_) {
    if (auto st = UploadRgbaTexture(soft_shadow_mask_, 12, white, 1, 1); !st) {
      return st;
    }
  }
  return Status::Ok();
}

Status D3D12Device::UploadProbeIrradianceAtlas(const float* rgb, int count, int nx, int ny,
                                               int nz) {
  if (!rgb || count <= 0 || nx <= 0 || ny <= 0 || nz <= 0) {
    return Status::Fail(ErrorCode::InvalidArgument, "UploadProbeIrradianceAtlas: invalid args");
  }
  if (count < nx * ny * nz) {
    return Status::Fail(ErrorCode::InvalidArgument, "UploadProbeIrradianceAtlas: count < nx*ny*nz");
  }
  const int w = nx;
  const int h = ny * nz;
  constexpr float kScale = 2.f;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w * h * 4));
  for (int z = 0; z < nz; ++z) {
    for (int y = 0; y < ny; ++y) {
      for (int x = 0; x < nx; ++x) {
        const int pi = x + nx * (y + ny * z);
        const std::size_t src = static_cast<std::size_t>(pi) * 3;
        const int dst_i = x + w * (y + z * ny);
        const std::size_t dst = static_cast<std::size_t>(dst_i) * 4;
        auto pack = [&](float v) -> std::uint8_t {
          const float t = (std::min)(1.f, (std::max)(0.f, v / kScale));
          return static_cast<std::uint8_t>(t * 255.f + 0.5f);
        };
        rgba[dst + 0] = pack(rgb[src + 0]);
        rgba[dst + 1] = pack(rgb[src + 1]);
        rgba[dst + 2] = pack(rgb[src + 2]);
        rgba[dst + 3] = 255;
      }
    }
  }
  return UploadRgbaTexture(probe_gi_atlas_, 11, rgba.data(), w, h);
}

Status D3D12Device::UploadSoftShadowMask(const float* factors, int width, int height) {
  if (!factors || width <= 0 || height <= 0) {
    return Status::Fail(ErrorCode::InvalidArgument, "UploadSoftShadowMask: invalid args");
  }
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height * 4));
  for (int i = 0; i < width * height; ++i) {
    const float t = (std::min)(1.f, (std::max)(0.f, factors[i]));
    const std::uint8_t u = static_cast<std::uint8_t>(t * 255.f + 0.5f);
    const std::size_t dst = static_cast<std::size_t>(i) * 4;
    rgba[dst + 0] = u;
    rgba[dst + 1] = u;
    rgba[dst + 2] = u;
    rgba[dst + 3] = 255;
  }
  return UploadRgbaTexture(soft_shadow_mask_, 12, rgba.data(), width, height);
}

Status D3D12Device::SetupSkybox(const std::filesystem::path& vs_dxil,
                   const std::filesystem::path& ps_dxil) {
  if (!device_ || vs_dxil.empty() || ps_dxil.empty()) {
    return Status::Fail("SetupSkybox: invalid");
  }
  std::ifstream vs_in(vs_dxil, std::ios::binary);
  std::ifstream ps_in(ps_dxil, std::ios::binary);
  if (!vs_in || !ps_in) {
    return Status::Fail("Skybox shaders missing");
  }
  std::vector<char> vs((std::istreambuf_iterator<char>(vs_in)), std::istreambuf_iterator<char>());
  std::vector<char> ps((std::istreambuf_iterator<char>(ps_in)), std::istreambuf_iterator<char>());
  if (vs.empty() || ps.empty()) {
    return Status::Fail("Skybox shaders empty");
  }

  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  range.BaseShaderRegister = 0;
  range.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &range;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC samp{};
  samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samp.ShaderRegister = 0;
  samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rs{};
  rs.NumParameters = 2;
  rs.pParameters = params;
  rs.NumStaticSamplers = 1;
  rs.pStaticSamplers = &samp;
  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
    return Status::Fail("Serialize sky root failed");
  }
  if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                          IID_PPV_ARGS(&sky_root_)))) {
    return Status::Fail("Create sky root failed");
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = sky_root_.Get();
  pso.VS = {vs.data(), vs.size()};
  pso.PS = {ps.data(), ps.size()};
  pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso.SampleMask = UINT_MAX;
  pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso.RasterizerState.DepthClipEnable = TRUE;
  pso.DepthStencilState.DepthEnable = TRUE;
  pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
  pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pso.SampleDesc.Count = 1;
  if (FAILED(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&sky_pso_)))) {
    // Fallback LDR swapchain format if HDR RT not used in this build path.
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&sky_pso_)))) {
      return Status::Fail("Create sky PSO failed");
    }
  }

  D3D12_DESCRIPTOR_HEAP_DESC heap{};
  heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap.NumDescriptors = 1;
  heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&sky_srv_heap_)))) {
    return Status::Fail("Create sky SRV heap failed");
  }

  D3D12_HEAP_PROPERTIES upload{};
  upload.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC cb{};
  cb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cb.Width = 256ull * kFrameCount;
  cb.Height = 1;
  cb.DepthOrArraySize = 1;
  cb.MipLevels = 1;
  cb.SampleDesc.Count = 1;
  cb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &cb,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(&sky_cb_)))) {
    return Status::Fail("Create sky CB failed");
  }
  sky_ready_ = true;
  return Status::Ok();
}

Status D3D12Device::UploadSkyCubemap(const std::uint8_t* rgba_faces, int face_size) {
  if (!sky_ready_) {
    return Status::Fail("SetupSkybox first");
  }
  if (auto st = UploadCubemapResource(sky_cube_, rgba_faces, face_size); !st) {
    return st;
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.TextureCube.MipLevels = 1;
  device_->CreateShaderResourceView(sky_cube_.Get(), &srv,
                                    sky_srv_heap_->GetCPUDescriptorHandleForHeapStart());
  sky_uploaded_ = true;
  engine::SetFeatureOverride("skybox", true);
  return Status::Ok();
}

Status D3D12Device::DrawSkybox(const Mat4& view_rot_proj) {
  if (!sky_ready_ || !sky_uploaded_ || !command_list_) {
    return Status::Ok();
  }
  BindSceneColorTargets();
  void* mapped = nullptr;
  if (FAILED(sky_cb_->Map(0, nullptr, &mapped)) || !mapped) {
    return Status::Fail("Map sky CB failed");
  }
  const UINT64 off = SkyCbOffset();
  std::memcpy(static_cast<char*>(mapped) + off, view_rot_proj.m.data(), sizeof(float) * 16);
  sky_cb_->Unmap(0, nullptr);

  command_list_->SetPipelineState(sky_pso_.Get());
  command_list_->SetGraphicsRootSignature(sky_root_.Get());
  ID3D12DescriptorHeap* heaps[] = {sky_srv_heap_.Get()};
  command_list_->SetDescriptorHeaps(1, heaps);
  command_list_->SetGraphicsRootConstantBufferView(0, sky_cb_->GetGPUVirtualAddress() + off);
  command_list_->SetGraphicsRootDescriptorTable(
      1, sky_srv_heap_->GetGPUDescriptorHandleForHeapStart());
  command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list_->DrawInstanced(36, 1, 0, 0);
  return Status::Ok();
}

Status D3D12Device::UploadCubemapResource(ComPtr<ID3D12Resource>& cube, const std::uint8_t* rgba_faces,
                             int face_size) {
  if (!rgba_faces || face_size <= 0 || !device_) {
    return Status::Fail("Invalid cubemap upload");
  }
  WaitGpu();
  cube.Reset();
  const UINT size = static_cast<UINT>(face_size);
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = size;
  desc.Height = size;
  desc.DepthOrArraySize = 6;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES default_heap{};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  HRESULT hr = device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&cube));
  if (FAILED(hr)) {
    return Status::Fail("Create cubemap failed: " + HrToString(hr));
  }
  UINT64 total_bytes = 0;
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 6> layouts{};
  std::array<UINT, 6> num_rows{};
  std::array<UINT64, 6> row_sizes{};
  device_->GetCopyableFootprints(&desc, 0, 6, 0, layouts.data(), num_rows.data(),
                                 row_sizes.data(), &total_bytes);
  D3D12_HEAP_PROPERTIES upload_heap{};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buf{};
  buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buf.Width = total_bytes;
  buf.Height = 1;
  buf.DepthOrArraySize = 1;
  buf.MipLevels = 1;
  buf.SampleDesc.Count = 1;
  buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> upload;
  hr = device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buf,
                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                        IID_PPV_ARGS(&upload));
  if (FAILED(hr)) {
    return Status::Fail("Create cubemap upload failed");
  }
  void* mapped = nullptr;
  if (FAILED(upload->Map(0, nullptr, &mapped)) || !mapped) {
    return Status::Fail("Map cubemap upload failed");
  }
  auto* dst = static_cast<std::uint8_t*>(mapped);
  for (UINT face = 0; face < 6; ++face) {
    const auto* src_face = rgba_faces + static_cast<std::size_t>(face) * size * size * 4;
    for (UINT row = 0; row < size; ++row) {
      std::memcpy(dst + layouts[face].Offset + row * layouts[face].Footprint.RowPitch,
                  src_face + row * size * 4, size * 4);
    }
  }
  upload->Unmap(0, nullptr);
  const auto frame = frame_index_;
  if (FAILED(allocators_[frame]->Reset()) ||
      FAILED(command_list_->Reset(allocators_[frame].Get(), nullptr))) {
    return Status::Fail("Cubemap command list reset failed");
  }
  for (UINT face = 0; face < 6; ++face) {
    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
    dst_loc.pResource = cube.Get();
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = face;
    D3D12_TEXTURE_COPY_LOCATION src_loc{};
    src_loc.pResource = upload.Get();
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = layouts[face];
    command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
  }
  Transition(cube.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  if (FAILED(command_list_->Close())) {
    return Status::Fail("Cubemap Close failed");
  }
  ID3D12CommandList* lists[] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  WaitGpu();
  return Status::Ok();
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
