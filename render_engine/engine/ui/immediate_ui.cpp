#include "engine/ui/immediate_ui.h"

#include "engine/core/log.h"

#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
#include "imgui.h"
#endif

#include <cstring>
#include <cstdint>
#include <vector>

namespace engine::ui {

struct ImmediateUi::Impl {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  bool ready = false;
  bool atlas_uploaded = false;
  bool capture_mouse = false;
  bool capture_keyboard = false;
#endif
};

ImmediateUi::ImmediateUi() : impl_(std::make_unique<Impl>()) {}
ImmediateUi::~ImmediateUi() {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  if (impl_ && impl_->ready) {
    ImGui::DestroyContext();
  }
#endif
}

bool ImmediateUi::available() const {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  return true;
#else
  return false;
#endif
}

Status ImmediateUi::Init(rhi::IDevice& device, const ImmediateUiDesc& desc) {
#if !(defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI)
  (void)device;
  (void)desc;
  return Status::Fail("ENGINE_WITH_IMGUI is disabled (imgui not found)");
#else
  if (desc.ui_vs.empty() || desc.ui_ps.empty()) {
    return Status::Fail("ImmediateUi requires ui shader paths");
  }
  rhi::SimpleMeshShaders shaders;
  shaders.vs_dxil = desc.ui_vs;
  shaders.ps_dxil = desc.ui_ps;
  if (auto st = device.SetupUiMesh(shaders); !st) {
    return st;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  io.IniFilename = nullptr;

  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 6.f;
  style.FrameRounding = 4.f;

  unsigned char* pixels = nullptr;
  int w = 0, h = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  if (!pixels || w <= 0 || h <= 0) {
    return Status::Fail("ImGui font atlas empty");
  }
  if (auto st = device.UploadUiFontAtlas(pixels, w, h); !st) {
    return st;
  }
  io.Fonts->SetTexID(static_cast<ImTextureID>(1));
  impl_->atlas_uploaded = true;
  impl_->ready = true;
  LogInfo("ImmediateUi (Dear ImGui) ready");
  return Status::Ok();
#endif
}

void ImmediateUi::BeginFrame(const WindowInputSnapshot& input, float display_w, float display_h,
                             float delta_time) {
#if !(defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI)
  (void)input;
  (void)display_w;
  (void)display_h;
  (void)delta_time;
#else
  if (!impl_->ready) {
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(display_w > 0.f ? display_w : 1.f, display_h > 0.f ? display_h : 1.f);
  io.DeltaTime = delta_time > 0.f ? delta_time : (1.f / 60.f);

  io.AddMousePosEvent(input.mouse_x, input.mouse_y);
  io.AddMouseButtonEvent(0, input.mouse_left);
  io.AddMouseButtonEvent(1, input.mouse_right);

  auto map_key = [](int vk) -> ImGuiKey {
    switch (vk) {
      case 0x08:
        return ImGuiKey_Backspace;
      case 0x09:
        return ImGuiKey_Tab;
      case 0x0D:
        return ImGuiKey_Enter;
      case 0x1B:
        return ImGuiKey_Escape;
      case 0x20:
        return ImGuiKey_Space;
      case 0x25:
        return ImGuiKey_LeftArrow;
      case 0x26:
        return ImGuiKey_UpArrow;
      case 0x27:
        return ImGuiKey_RightArrow;
      case 0x28:
        return ImGuiKey_DownArrow;
      case 0x70:
        return ImGuiKey_F1;
      default:
        if (vk >= 'A' && vk <= 'Z') {
          return static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A'));
        }
        if (vk >= '0' && vk <= '9') {
          return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0'));
        }
        return ImGuiKey_None;
    }
  };
  for (int vk = 0; vk < 256; ++vk) {
    const ImGuiKey key = map_key(vk);
    if (key != ImGuiKey_None) {
      io.AddKeyEvent(key, input.keys[static_cast<std::size_t>(vk)]);
    }
  }

  ImGui::NewFrame();
  impl_->capture_mouse = io.WantCaptureMouse;
  impl_->capture_keyboard = io.WantCaptureKeyboard;
#endif
}

Status ImmediateUi::Render(rhi::IDevice& device) {
#if !(defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI)
  (void)device;
  return Status::Ok();
#else
  if (!impl_->ready) {
    return Status::Fail("ImmediateUi not initialized");
  }
  ImGui::Render();
  ImDrawData* draw = ImGui::GetDrawData();
  if (!draw || draw->CmdListsCount == 0) {
    return Status::Ok();
  }

  std::vector<rhi::UiVertex> vertices;
  std::vector<std::uint16_t> indices;
  std::vector<rhi::UiDrawCmd> commands;
  vertices.reserve(static_cast<std::size_t>(draw->TotalVtxCount));
  indices.reserve(static_cast<std::size_t>(draw->TotalIdxCount));

  for (int n = 0; n < draw->CmdListsCount; ++n) {
    const ImDrawList* list = draw->CmdLists[n];
    const int vtx_base = static_cast<int>(vertices.size());
    for (int i = 0; i < list->VtxBuffer.Size; ++i) {
      const ImDrawVert& v = list->VtxBuffer[i];
      rhi::UiVertex out;
      out.x = v.pos.x;
      out.y = v.pos.y;
      out.u = v.uv.x;
      out.v = v.uv.y;
      const auto c = v.col;
      out.r = static_cast<float>((c >> 0) & 0xFF) / 255.f;
      out.g = static_cast<float>((c >> 8) & 0xFF) / 255.f;
      out.b = static_cast<float>((c >> 16) & 0xFF) / 255.f;
      out.a = static_cast<float>((c >> 24) & 0xFF) / 255.f;
      vertices.push_back(out);
    }
    const int idx_base = static_cast<int>(indices.size());
    for (int i = 0; i < list->IdxBuffer.Size; ++i) {
      indices.push_back(static_cast<std::uint16_t>(list->IdxBuffer[i] + vtx_base));
    }
    for (int cmd_i = 0; cmd_i < list->CmdBuffer.Size; ++cmd_i) {
      const ImDrawCmd& cmd = list->CmdBuffer[cmd_i];
      if (cmd.UserCallback) {
        continue;
      }
      rhi::UiDrawCmd out_cmd;
      out_cmd.index_offset = static_cast<std::uint32_t>(idx_base + static_cast<int>(cmd.IdxOffset));
      out_cmd.index_count = cmd.ElemCount;
      out_cmd.clip_x0 = cmd.ClipRect.x;
      out_cmd.clip_y0 = cmd.ClipRect.y;
      out_cmd.clip_x1 = cmd.ClipRect.z;
      out_cmd.clip_y1 = cmd.ClipRect.w;
      commands.push_back(out_cmd);
    }
  }

  return device.DrawUiMesh(vertices, indices, commands);
#endif
}

bool ImmediateUi::want_capture_mouse() const {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  return impl_ && impl_->capture_mouse;
#else
  return false;
#endif
}

bool ImmediateUi::want_capture_keyboard() const {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  return impl_ && impl_->capture_keyboard;
#else
  return false;
#endif
}

bool ImmediateUi::BeginWindow(std::string_view title, float x, float y, float w, float h) {
#if !(defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI)
  (void)title;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  return false;
#else
  if (!impl_->ready) {
    return false;
  }
  ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
  return ImGui::Begin(std::string(title).c_str(), nullptr, ImGuiWindowFlags_None);
#endif
}

void ImmediateUi::EndWindow() {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  if (impl_->ready) {
    ImGui::End();
  }
#endif
}

void ImmediateUi::Text(std::string_view text) {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  if (impl_->ready) {
    ImGui::TextUnformatted(std::string(text).c_str());
  }
#else
  (void)text;
#endif
}

void ImmediateUi::Separator() {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  if (impl_->ready) {
    ImGui::Separator();
  }
#endif
}

bool ImmediateUi::Checkbox(std::string_view label, bool* value) {
#if !(defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI)
  (void)label;
  (void)value;
  return false;
#else
  if (!impl_->ready || !value) {
    return false;
  }
  return ImGui::Checkbox(std::string(label).c_str(), value);
#endif
}

bool ImmediateUi::SliderFloat(std::string_view label, float* value, float min_v, float max_v) {
#if !(defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI)
  (void)label;
  (void)value;
  (void)min_v;
  (void)max_v;
  return false;
#else
  if (!impl_->ready || !value) {
    return false;
  }
  return ImGui::SliderFloat(std::string(label).c_str(), value, min_v, max_v);
#endif
}

bool ImmediateUi::SliderInt(std::string_view label, int* value, int min_v, int max_v) {
#if !(defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI)
  (void)label;
  (void)value;
  (void)min_v;
  (void)max_v;
  return false;
#else
  if (!impl_->ready || !value) {
    return false;
  }
  return ImGui::SliderInt(std::string(label).c_str(), value, min_v, max_v);
#endif
}

bool ImmediateUi::Button(std::string_view label, float w, float h) {
#if !(defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI)
  (void)label;
  (void)w;
  (void)h;
  return false;
#else
  if (!impl_->ready) {
    return false;
  }
  return ImGui::Button(std::string(label).c_str(), ImVec2(w, h));
#endif
}

}  // namespace engine::ui
