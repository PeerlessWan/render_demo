// Screen quads (NDC) for Vulkan SPIR-V.

struct QuadIn {
  float2 ndc : POSITION;
  float4 color : COLOR;
};

struct QuadOut {
  float4 pos : SV_Position;
  float4 color : COLOR;
};

QuadOut VSMain(QuadIn input) {
  QuadOut o;
  o.pos = float4(input.ndc, 0.0f, 1.0f);
  o.color = input.color;
  return o;
}

float4 PSMain(QuadOut input) : SV_Target { return input.color; }
