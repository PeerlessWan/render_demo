// Screen-space post: SSAO + TAA + SSR + auto-exposure + bloom + fog + tonemap
cbuffer PostCB : register(b0) {
  float2 g_inv_res;
  float g_enable_ssao;
  float g_enable_taa;

  float g_ssao_radius;
  float g_ssao_intensity;
  float g_taa_blend;
  float g_exposure;

  float4x4 g_inv_view_proj;
  float4x4 g_view_proj;

  float3 g_eye;
  float g_tonemap_mode; // 0=none 1=reinhard 2=ACES

  float g_enable_auto_exposure;
  float g_auto_exposure_key;
  float g_enable_bloom;
  float g_bloom_threshold;

  float g_bloom_intensity;
  float g_enable_fog;
  float g_fog_density;
  float g_fog_start;

  float3 g_fog_color;
  float g_enable_tonemap;

  float g_enable_ssr;
  float g_ssr_intensity;
  float g_ssr_thickness;
  float g_enable_dof;

  float g_dof_focus;
  float g_dof_scale;
  float g_enable_motion_blur;
  float g_motion_blur_strength;
  float4x4 g_prev_view_proj;
  float g_jitter_x;
  float g_jitter_y;
  float g_vignette_strength;
  float g_film_grain_strength;
  float g_chromatic_aberration;
  float g_lens_distortion;
  float g_light_dirt_strength;
  float g_flare_strength;
  float g_enable_gtao;
  float g_enable_fxaa;
  float g_enable_color_grading;
  float g_color_grading_strength;
  float3 g_fog_box_min;
  float g_enable_fog_box;
  float3 g_fog_box_max;
  float g_ssr_roughness_fade;
};

Texture2D g_scene_color : register(t0);
Texture2D g_scene_depth : register(t1);
Texture2D g_history : register(t2);
SamplerState g_linear : register(s0);
SamplerState g_point : register(s1);

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float Luma(float3 c) {
  return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 ReconstructWorld(float2 uv, float depth) {
  float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
  clip.y *= -1.0;
  float4 world = mul(g_inv_view_proj, clip);
  return world.xyz / max(world.w, 1e-5);
}

float2 WorldToUv(float3 world, out float ndc_z) {
  float4 clip = mul(g_view_proj, float4(world, 1.0));
  float3 ndc = clip.xyz / max(clip.w, 1e-5);
  ndc_z = ndc.z;
  return ndc.xy * float2(0.5, -0.5) + 0.5;
}

float GTAO(float2 uv, float3 origin, float3 normal) {
  // Ground-truth AO-lite: horizon search along a few slices (teaching / product deepen).
  float ao = 0.0;
  const int kSlices = 4;
  const int kSteps = 4;
  [unroll] for (int s = 0; s < kSlices; ++s) {
    float ang = (float)s * 3.14159265 / (float)kSlices;
    float2 dir = float2(cos(ang), sin(ang));
    float h = -1.0;
    [unroll] for (int t = 1; t <= kSteps; ++t) {
      float2 suv = saturate(uv + dir * g_inv_res * (g_ssao_radius * 0.35 * (float)t));
      float sd = g_scene_depth.Sample(g_point, suv).r;
      if (sd >= 0.9999) continue;
      float3 sp = ReconstructWorld(suv, sd);
      float3 v = sp - origin;
      float dist = length(v);
      float3 vd = v / max(dist, 1e-5);
      float hz = dot(vd, normal);
      h = max(h, hz);
    }
    ao += saturate(1.0 - max(h, 0.0));
  }
  return lerp(1.0, ao / (float)kSlices, saturate(g_ssao_intensity));
}

float3 Fxaa(float2 uv, float3 color) {
  float3 nw = g_scene_color.Sample(g_linear, saturate(uv + float2(-1, -1) * g_inv_res)).rgb;
  float3 ne = g_scene_color.Sample(g_linear, saturate(uv + float2(1, -1) * g_inv_res)).rgb;
  float3 sw = g_scene_color.Sample(g_linear, saturate(uv + float2(-1, 1) * g_inv_res)).rgb;
  float3 se = g_scene_color.Sample(g_linear, saturate(uv + float2(1, 1) * g_inv_res)).rgb;
  float luma = Luma(color);
  float luma_min = min(luma, min(min(Luma(nw), Luma(ne)), min(Luma(sw), Luma(se))));
  float luma_max = max(luma, max(max(Luma(nw), Luma(ne)), max(Luma(sw), Luma(se))));
  float range = luma_max - luma_min;
  if (range < 0.0312) return color;
  float3 blur = (nw + ne + sw + se) * 0.25;
  return lerp(color, blur, saturate(range * 4.0));
}

float3 ApplyColorGrade(float3 color) {
  // Identity 3D LUT stand-in: mild contrast lift when grading enabled (no texture bound).
  float3 graded = saturate(color * 1.02 - 0.01);
  graded = lerp(color, graded, 0.35);
  return lerp(color, graded, saturate(g_color_grading_strength));
}

float SSAO(float2 uv, float3 origin, float3 normal) {
  float occl = 0.0;
  [unroll] for (int i = 0; i < 8; ++i) {
    float a = (float)i * 2.399963;
    float r = g_ssao_radius * (0.25 + 0.75 * (float)i / 7.0);
    float2 offset = float2(cos(a), sin(a)) * r * g_inv_res;
    float2 suv = saturate(uv + offset);
    float sd = g_scene_depth.Sample(g_point, suv).r;
    if (sd >= 0.9999) {
      continue;
    }
    float3 sample_pos = ReconstructWorld(suv, sd);
    float3 v = sample_pos - origin;
    float dist = length(v);
    float3 dir = v / max(dist, 1e-5);
    float nd = saturate(dot(normal, dir));
    float atten = 1.0 - saturate(dist / (g_ssao_radius * 25.0));
    occl += nd * atten;
  }
  float ao = 1.0 - saturate(occl / 8.0) * g_ssao_intensity;
  return ao;
}

float EstimateAvgLuma() {
  // Only average geometry pixels. Sampling the clear-color sky makes auto-exposure
  // explode when the floor fills the frame after a dark-sky view → floor goes white.
  float sum = 0.0;
  int count = 0;
  [unroll] for (int y = 0; y < 4; ++y) {
    [unroll] for (int x = 0; x < 4; ++x) {
      float2 uv = (float2(x, y) + 0.5) * 0.25;
      float d = g_scene_depth.SampleLevel(g_point, uv, 0).r;
      if (d >= 0.9995) {
        continue;
      }
      float3 c = g_scene_color.SampleLevel(g_linear, uv, 0).rgb;
      sum += log2(max(Luma(c), 1e-4));
      count += 1;
    }
  }
  if (count < 1) {
    return 0.18;
  }
  return exp2(sum / (float)count);
}

float3 ACESFilm(float3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 SoftBloom(float2 uv, float3 color) {
  float thr = max(g_bloom_threshold, 0.35);
  float3 bright = max(color - thr, 0.0);
  // Only bloom small hot spots (emissive / specular). Large floor regions above thr
  // used to smear into a floating white sheet across the mid-screen horizon.
  float hot = max(bright.r, max(bright.g, bright.b));
  if (hot < 0.02) {
    return 0.0.xxx;
  }
  float3 acc = bright;
  const float2 offs[8] = {
      float2(1, 0),  float2(-1, 0), float2(0, 1),  float2(0, -1),
      float2(1, 1),  float2(1, -1), float2(-1, 1), float2(-1, -1),
  };
  [unroll] for (int i = 0; i < 8; ++i) {
    float2 suv = saturate(uv + offs[i] * g_inv_res * 2.5);
    float3 s = g_scene_color.Sample(g_linear, suv).rgb;
    acc += max(s - thr, 0.0) * 0.55;
    float2 suv2 = saturate(uv + offs[i] * g_inv_res * 5.0);
    float3 s2 = g_scene_color.Sample(g_linear, suv2).rgb;
    acc += max(s2 - thr, 0.0) * 0.25;
  }
  return (acc / 9.0) * saturate(hot * 2.0);
}

float3 TraceSSR(float2 uv, float3 origin, float3 normal) {
  float3 view_dir = normalize(origin - g_eye);
  float3 refl = normalize(reflect(view_dir, normal));
  // Skip rays pointing into the surface / toward camera too strongly.
  if (dot(refl, normal) < 0.05) {
    return 0.0.xxx;
  }

  float step_len = 0.35;
  float3 pos = origin + refl * 0.05;
  float2 hit_uv = uv;
  bool hit = false;
  [loop] for (int i = 0; i < 24; ++i) {
    pos += refl * step_len;
    step_len *= 1.08;
    float ndc_z;
    float2 suv = WorldToUv(pos, ndc_z);
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) {
      break;
    }
    float scene_d = g_scene_depth.Sample(g_point, suv).r;
    if (scene_d >= 0.9999) {
      continue;
    }
    float thickness = max(g_ssr_thickness, 0.002);
    if (ndc_z > scene_d && ndc_z < scene_d + thickness) {
      hit = true;
      hit_uv = suv;
      break;
    }
    // Passed through geometry — binary refine once.
    if (ndc_z > scene_d + thickness) {
      pos -= refl * step_len * 0.5;
      float ndc_z2;
      float2 suv2 = WorldToUv(pos, ndc_z2);
      float scene_d2 = g_scene_depth.Sample(g_point, suv2).r;
      if (ndc_z2 > scene_d2 && ndc_z2 < scene_d2 + thickness * 2.0) {
        hit = true;
        hit_uv = suv2;
      }
      break;
    }
  }

  if (!hit) {
    return 0.0.xxx;
  }
  float edge = saturate(1.0 - max(abs(hit_uv.x - 0.5), abs(hit_uv.y - 0.5)) * 2.0);
  edge = smoothstep(0.0, 0.35, edge);
  float fresnel = pow(1.0 - saturate(dot(-view_dir, normal)), 3.0);
  float3 reflected = g_scene_color.Sample(g_linear, hit_uv).rgb;
  return reflected * edge * (0.25 + 0.75 * fresnel);
}

float4 PSMain(VSOut input) : SV_Target {
  float2 uv = input.uv;
  float3 color = g_scene_color.Sample(g_linear, uv).rgb;
  float depth = g_scene_depth.Sample(g_point, uv).r;

  float3 origin = 0.0.xxx;
  float3 normal = float3(0, 1, 0);
  bool has_surface = (depth < 0.9999);
  if (has_surface) {
    origin = ReconstructWorld(uv, depth);
    float2 texel = g_inv_res;
    float3 p1 = ReconstructWorld(uv + float2(texel.x, 0),
                                 g_scene_depth.Sample(g_point, uv + float2(texel.x, 0)).r);
    float3 p2 = ReconstructWorld(uv + float2(0, texel.y),
                                 g_scene_depth.Sample(g_point, uv + float2(0, texel.y)).r);
    normal = normalize(cross(p1 - origin, p2 - origin));
  }

  if (g_enable_gtao > 0.5 && has_surface) {
    color *= GTAO(uv, origin, normal);
  } else if (g_enable_ssao > 0.5 && has_surface) {
    color *= SSAO(uv, origin, normal);
  }

  if (g_enable_ssr > 0.5 && has_surface) {
    // Horizontal floors produce unstable SSR hits → sparkle / holes; fade them.
    float upright = saturate(abs(normal.y));
    float rough_fade = saturate(1.0 - upright * g_ssr_roughness_fade);
    float ssr_w = max(g_ssr_intensity, 0.0) * (1.0 - upright * 0.85) * rough_fade;
    if (ssr_w > 1e-3) {
      color += TraceSSR(uv, origin, normal) * ssr_w;
    }
  }

  if (g_enable_dof > 0.5 && has_surface) {
    float dist = length(origin - g_eye);
    float coc = saturate(abs(dist - g_dof_focus) * max(g_dof_scale, 0.0));
    float3 blur = color;
    [unroll] for (int i = 0; i < 8; ++i) {
      float a = (float)i * 2.399963;
      float2 offset = float2(cos(a), sin(a)) * g_inv_res * (1.5 + 6.0 * coc);
      blur += g_scene_color.Sample(g_linear, saturate(uv + offset)).rgb;
    }
    blur /= 9.0;
    color = lerp(color, blur, coc);
  }

  if (g_enable_taa > 0.5) {
    // Camera motion vectors from depth reprojection (static geometry).
    float2 hist_uv = uv;
    if (has_surface) {
      float4 prev_clip = mul(g_prev_view_proj, float4(origin, 1.0));
      float2 prev_ndc = prev_clip.xy / max(prev_clip.w, 1e-5);
      hist_uv = prev_ndc * float2(0.5, -0.5) + 0.5;
      // Undo current jitter so history lines up with unjittered UV.
      hist_uv -= float2(g_jitter_x, -g_jitter_y) * 0.5;
    }
    hist_uv = saturate(hist_uv);
    float3 hist = g_history.Sample(g_linear, hist_uv).rgb;
    float3 cmin = color;
    float3 cmax = color;
    [unroll] for (int y = -1; y <= 1; ++y) {
      [unroll] for (int x = -1; x <= 1; ++x) {
        float3 n = g_scene_color.Sample(g_point, uv + float2(x, y) * g_inv_res).rgb;
        cmin = min(cmin, n);
        cmax = max(cmax, n);
      }
    }
    hist = clamp(hist, cmin, cmax);
    float blend = g_taa_blend;
    float2 mv = hist_uv - uv;
    float mv_len = length(mv);
    if (mv_len > 0.08) {
      blend *= 0.25;  // fast camera motion
    }
    float luma_delta = abs(Luma(color) - Luma(hist));
    if (luma_delta > 0.35) {
      blend *= 0.15;  // disocclusion / big lighting change
    }
    color = lerp(color, hist, saturate(blend));
  } else if (g_enable_fxaa > 0.5) {
    color = Fxaa(uv, color);
  } else if (g_enable_motion_blur > 0.5) {
    // Cheap camera-motion stand-in: blend toward last resolved frame.
    float3 hist = g_history.Sample(g_linear, uv).rgb;
    color = lerp(color, hist, saturate(g_motion_blur_strength));
  }

  if (g_enable_bloom > 0.5) {
    color += SoftBloom(uv, color) * max(g_bloom_intensity, 0.0);
  }

  float exposure = max(g_exposure, 0.0);
  if (g_enable_auto_exposure > 0.5) {
    float avg = EstimateAvgLuma();
    float target = max(g_auto_exposure_key, 0.01);
    float auto_exp = target / max(avg, 1e-3);
    // Tight clamp — wide range was blowing the floor to white at some views.
    auto_exp = clamp(auto_exp, 0.75, 1.35);
    exposure = lerp(exposure, exposure * auto_exp, 0.2);
  }
  color *= exposure;

  if (g_enable_fog > 0.5 && has_surface) {
    float dist = length(origin - g_eye);
    float fog_d = max(0.0, dist - max(g_fog_start, 0.0));
    float fog = 1.0 - exp(-max(g_fog_density, 0.0) * fog_d);
    float box_w = 1.0;
    if (g_enable_fog_box > 0.5) {
      float3 t = saturate((origin - g_fog_box_min) / max(g_fog_box_max - g_fog_box_min, 1e-3));
      // Inside AABB → full fog weight; outside → attenuate.
      float inside = (origin.x >= g_fog_box_min.x && origin.x <= g_fog_box_max.x &&
                      origin.y >= g_fog_box_min.y && origin.y <= g_fog_box_max.y &&
                      origin.z >= g_fog_box_min.z && origin.z <= g_fog_box_max.z)
                         ? 1.0
                         : 0.15;
      box_w = inside;
      (void)t;
    }
    color = lerp(color, g_fog_color, saturate(fog * box_w));
  }

  if (g_enable_color_grading > 0.5) {
    color = ApplyColorGrade(color);
  }

  if (g_enable_tonemap > 0.5) {
    if (g_tonemap_mode < 0.5) {
      // linear
    } else if (g_tonemap_mode < 1.5) {
      color = color / (1.0 + color);
    } else {
      color = ACESFilm(color);
    }
  } else {
    color = saturate(color);
  }

  // M26/C04: vignette + film grain (strength 0 = off).
  if (g_vignette_strength > 1e-4) {
    float2 d = uv * 2.0 - 1.0;
    float r2 = dot(d, d);
    float vig = saturate(1.0 - r2 * g_vignette_strength);
    color *= vig;
  }
  if (g_film_grain_strength > 1e-4) {
    float n = frac(sin(dot(uv * 1129.0 + g_eye.xy, float2(12.9898, 78.233))) * 43758.5453);
    color += (n - 0.5) * g_film_grain_strength;
  }

  // W7/C04: simple chromatic aberration (RGB sample offset from center).
  if (g_chromatic_aberration > 1e-4) {
    float2 from_c = uv - 0.5;
    float2 dir = from_c * g_chromatic_aberration * 0.02;
    float3 split;
    split.r = g_scene_color.Sample(g_linear, saturate(uv + dir)).r;
    split.g = color.g;
    split.b = g_scene_color.Sample(g_linear, saturate(uv - dir)).b;
    // Re-apply only chroma; keep tonemap/fog already baked into green path approx.
    color = lerp(color, split, saturate(g_chromatic_aberration));
  }

  // Mega-W8 C04: mild barrel/pincushion resample (strength 0 = off).
  if (g_lens_distortion > 1e-4) {
    float2 from_c = uv - 0.5;
    float r2 = dot(from_c, from_c);
    float2 duv = from_c * (1.0 + g_lens_distortion * r2);
    float3 distorted = g_scene_color.Sample(g_linear, saturate(duv + 0.5)).rgb;
    color = lerp(color, distorted, saturate(g_lens_distortion));
  }

  // Mega-W8 C04: cheap dirt / anamorphic flare stubs (screen-space, strength 0 = off).
  if (g_light_dirt_strength > 1e-4 || g_flare_strength > 1e-4) {
    float2 from_c = uv - 0.5;
    float dirt = saturate(1.0 - length(from_c) * 1.4);
    dirt *= dirt;
    float grain = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
    color += dirt * g_light_dirt_strength * (0.55 + 0.45 * grain) * float3(1.0, 0.92, 0.85);
    float streak = saturate(1.0 - abs(from_c.y) * 8.0) * saturate(1.0 - abs(from_c.x) * 1.2);
    color += streak * g_flare_strength * float3(1.0, 0.85, 0.55);
  }

  return float4(color, 1.0);
}
