#pragma once

// Sandbox UI copy: English / 简体中文. Technical acronyms kept bilingual where useful.

enum class SandboxUiLang : int { En = 0, Zh = 1 };

struct SandboxUiStrings {
  const char* language;
  const char* lang_en;
  const char* lang_zh;
  const char* perf;
  const char* effects;
  const char* hint;
  const char* profiler;
  const char* help_look;
  const char* help_move;
  const char* show_grid;
  const char* show_axes;
  const char* probe_gi;
  const char* morph_bulge;
  const char* morph_squash;
  const char* shadows;
  const char* ssao;
  const char* taa;
  const char* ibl;
  const char* skybox;
  const char* reflection_probe;
  const char* ssr;
  const char* dof;
  const char* motion_blur;
  const char* tonemap;
  const char* auto_exposure;
  const char* bloom;
  const char* fog;
  const char* vsync;
  const char* sun_intensity;
  const char* ambient_scale;
  const char* exposure;
  const char* tonemap_mode;
  const char* ssr_intensity;
  const char* dof_focus;
  const char* dof_scale;
  const char* motion_blur_strength;
  const char* bloom_thr;
  const char* bloom_int;
  const char* fog_density;
  const char* fog_start;
  const char* shadow_bias;
  const char* specular_power;
  const char* local_light_scale;
  const char* ibl_intensity;
  const char* reflection_intensity;
  const char* shadow_cascades;
  const char* quality_low;
  const char* quality_med;
  const char* quality_high;
  const char* quit;
  const char* hint_keys;
  const char* cpu_scopes;
  const char* fps;
  const char* frame_ms;
  const char* cpu_pct;
  const char* working_set;
  const char* private_mem;
  const char* peak_ws;
  const char* page_faults;
};

inline const SandboxUiStrings& SandboxUi(SandboxUiLang lang) {
  static const SandboxUiStrings kEn{
      "Language",
      "English",
      "Chinese",
      "Perf",
      "Effects",
      "Hint",
      "Profiler",
      "LMB/RMB drag look | Wheel zoom | MMB pan",
      "WASD/QE | Shift | F1 FX | F2 Profiler | F3 grid",
      "Show grid (F3)",
      "Show axes (F4)",
      "Probe GI",
      "Morph bulge",
      "Morph squash",
      "Shadows",
      "SSAO",
      "TAA",
      "IBL",
      "Skybox",
      "Reflection probe",
      "SSR",
      "DoF",
      "MotionBlur",
      "Tonemap",
      "AutoExposure",
      "Bloom",
      "Fog",
      "VSync",
      "Sun intensity",
      "Ambient scale",
      "Exposure",
      "Tonemap mode",
      "SSR intensity",
      "DoF focus",
      "DoF scale",
      "Motion blur",
      "Bloom thr",
      "Bloom int",
      "Fog density",
      "Fog start",
      "Shadow bias",
      "Specular power",
      "Local light scale",
      "IBL intensity",
      "Reflection intensity",
      "Shadow cascades",
      "Low",
      "Med",
      "High",
      "Quit",
      "F1 FX | F2 Profiler | F3 Grid | F4 Axes",
      "CPU scopes (1s)",
      "FPS",
      "Frame",
      "CPU",
      "WS",
      "Private",
      "Peak WS",
      "PageFaults",
  };
  static const SandboxUiStrings kZh{
      "语言",
      "English",
      "中文",
      "性能",
      "效果",
      "提示",
      "性能分析",
      "左键/右键拖拽视角 | 滚轮缩放 | 中键平移",
      "WASD/QE 移动 | Shift 加速 | F1 效果 | F2 分析 | F3 网格",
      "显示网格 (F3)",
      "显示坐标轴 (F4)",
      "探针 GI",
      "变形鼓起",
      "变形压扁",
      "阴影",
      "SSAO 环境光遮蔽",
      "TAA 抗锯齿",
      "IBL 环境光",
      "天空盒",
      "反射探针",
      "SSR 屏幕反射",
      "景深 DoF",
      "运动模糊",
      "色调映射",
      "自动曝光",
      "Bloom 光晕",
      "雾",
      "垂直同步",
      "太阳强度",
      "环境光倍率",
      "曝光",
      "色调映射模式",
      "SSR 强度",
      "景深对焦",
      "景深强度",
      "运动模糊强度",
      "光晕阈值",
      "光晕强度",
      "雾密度",
      "雾起始距离",
      "阴影偏移",
      "高光幂次",
      "本地光倍率",
      "IBL 强度",
      "反射强度",
      "阴影级联数",
      "低",
      "中",
      "高",
      "退出",
      "F1 效果 | F2 分析 | F3 网格 | F4 坐标轴",
      "CPU 作用域 (1秒)",
      "帧率",
      "帧时间",
      "CPU",
      "工作集",
      "私有内存",
      "峰值工作集",
      "缺页次数",
  };
  return lang == SandboxUiLang::Zh ? kZh : kEn;
}
