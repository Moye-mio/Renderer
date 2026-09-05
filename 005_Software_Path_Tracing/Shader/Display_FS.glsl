#version 430 core

// 把累积缓冲里的线性均值做曝光 + 色调映射 + gamma 后写回 backbuffer。
//
// 之所以和累积分成两趟：累积缓冲存的一直是线性能量，改曝光或换色调映射
// 不需要推倒重算，样本数照常往上涨。

layout(location = 0) in  vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_Accumulation;

LAYOUT_BIND(0, 1) layout(std140) uniform u_Display
{
	vec4 u_Params; // x: 曝光, y: 色调映射方式, z: gamma, w: 直通开关
};

// 与 RayTracingContext.h 的 RTToneMap 一一对应。
const int kToneMapNone     = 0;
const int kToneMapReinhard = 1;
const int kToneMapAces     = 2;

// Narkowicz 2015 的 ACES 近似拟合，一条曲线搞定，够看。
vec3 ToneMapAces(vec3 c)
{
	const float a = 2.51;
	const float b = 0.03;
	const float d = 2.43;
	const float e = 0.59;
	const float f = 0.14;
	return clamp((c * (a * c + b)) / (c * (d * c + e) + f), 0.0, 1.0);
}

void main()
{
	vec3 color = texture(u_Accumulation, v2f_TexCoords).rgb;

	// 法线可视化模式存的是编码后的方向而不是线性辐射亮度，曝光 / 色调映射 /
	// gamma 套上去只会把它拧成看不出朝向的颜色，所以直接原样输出。
	if (u_Params.w > 0.5)
	{
		Color_ = vec4(color, 1.0);
		return;
	}

	color *= max(u_Params.x, 0.0);

	int toneMap = int(u_Params.y);
	if (toneMap == kToneMapReinhard)
		color = color / (1.0 + color);
	else if (toneMap == kToneMapAces)
		color = ToneMapAces(color);

	color = pow(max(color, vec3(0.0)), vec3(1.0 / max(u_Params.z, 1e-3)));
	Color_ = vec4(color, 1.0);
}
