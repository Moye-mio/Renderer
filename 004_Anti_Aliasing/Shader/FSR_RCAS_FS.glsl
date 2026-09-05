#version 430 core

// FSR 1.0 的 RCAS（Robust Contrast Adaptive Sharpening）：在已经是显示分辨率的图上再锐一次。
// EASU 放大后整体偏软，这里用十字 5 点邻域做带负瓣的高通，
// 负瓣强度由"再锐多少才不会撞到 0/1、也不会越过邻域极值"反推，因此不会一刀切地锐化平坦区。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_InputColor;

LAYOUT_BIND(0, 1) layout(std140) uniform u_RCASParams
{
	vec4 u_Params; // x: 线性锐化强度 exp2(-stops)，<=0 表示直通；yz: 显示分辨率
};

// 0.25 - 1/16：限制负瓣不能太猛，否则边缘会出现不自然的光晕。
const float kRcasLimit = 0.25 - 1.0 / 16.0;

void main()
{
	//     b
	//   d e f
	//     h
	ivec2 lim = ivec2(u_Params.yz) - 1;
	ivec2 p = clamp(ivec2(v2f_TexCoords * u_Params.yz), ivec2(0), lim);

	vec3 e = texelFetch(u_InputColor, p, 0).rgb;
	if (u_Params.x <= 0.0)
	{
		Color_ = vec4(e, 1.0);
		return;
	}

	vec3 b = texelFetch(u_InputColor, clamp(p + ivec2( 0, -1), ivec2(0), lim), 0).rgb;
	vec3 d = texelFetch(u_InputColor, clamp(p + ivec2(-1,  0), ivec2(0), lim), 0).rgb;
	vec3 f = texelFetch(u_InputColor, clamp(p + ivec2( 1,  0), ivec2(0), lim), 0).rgb;
	vec3 h = texelFetch(u_InputColor, clamp(p + ivec2( 0,  1), ivec2(0), lim), 0).rgb;

	// 只看环上四点的极值，中心 e 不参与，这样过冲判据才是"相对邻域"
	vec3 mn4 = min(min(b, d), min(f, h));
	vec3 mx4 = max(max(b, d), max(f, h));

	vec3 hitMin = mn4 / max(4.0 * mx4, vec3(1e-5));
	vec3 hitMax = (1.0 - mx4) / min(4.0 * mn4 - 4.0, vec3(-1e-5));
	vec3 lobeRGB = max(-hitMin, hitMax);

	// 取三通道里最保守的一档，钳到 [-kRcasLimit, 0]，再乘用户锐化强度
	float lobe = max(-kRcasLimit,
	                 min(max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b), 0.0)) * u_Params.x;

	// lobe < 0 时这是高通：中心加重、四周减轻
	Color_ = vec4((e + lobe * (b + d + f + h)) / (1.0 + 4.0 * lobe), 1.0);
}
