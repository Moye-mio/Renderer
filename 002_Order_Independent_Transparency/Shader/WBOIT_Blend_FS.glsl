#version 430 core

// Weighted Blended OIT 全屏合成（对应文档 Custom/OITBlend）：
//   C = accum.rgb / clamp(accum.a, 1e-4, 5e4)
//   r = revealage
//   C_final = (1-r)*C + r*C_bg
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_AccumTex;
LAYOUT_BIND(0, 1) uniform sampler2D u_RevealageTex;
LAYOUT_BIND(0, 2) uniform sampler2D u_SceneColorTex;

void main()
{
	vec4 accum = texture(u_AccumTex, v2f_TexCoords);
	float revealage = texture(u_RevealageTex, v2f_TexCoords).r;
	vec3 background = texture(u_SceneColorTex, v2f_TexCoords).rgb;
	vec3 c = accum.rgb / clamp(accum.a, 1e-4, 5e4);
	Color_ = vec4((1.0 - revealage) * c + revealage * background, 1.0);
}
