#version 430 core

// Fourier Opacity OIT — Pass 4：全屏合成。
//   C = sum(w_i*c_i) / sum(w_i) * (1 - T_total) + C_opaque * T_total
// 整条视线积完后谐波项全部抵消（sin(2PI k)=0、1-cos(2PI k)=0），总透射率只剩
// DC 项 exp(-a0) = prod(1-alpha_i)，与 WBOIT 的 revealage 是同一个量。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_AccumTex;
LAYOUT_BIND(0, 1) uniform sampler2D u_CoefficientOneTex;
LAYOUT_BIND(0, 2) uniform sampler2D u_SceneColorTex;

void main()
{
	vec4 accum = texture(u_AccumTex, v2f_TexCoords);
	float totalOpticalDepth = texture(u_CoefficientOneTex, v2f_TexCoords).y;
	vec3 background = texture(u_SceneColorTex, v2f_TexCoords).rgb;

	float totalTransmittance = clamp(exp(-totalOpticalDepth), 0.0, 1.0);
	// 该像素没有任何半透明片元时 accum.a 为 0，直接回退到不透明底色。
	vec3 finalColor = background;
	if (accum.a > 1e-5)
	{
		vec3 averaged = accum.rgb / accum.a;
		finalColor = averaged * (1.0 - totalTransmittance) + background * totalTransmittance;
	}
	Color_ = vec4(finalColor, 1.0);
}
