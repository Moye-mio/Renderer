#version 430 core

// Weighted Blended OIT Accumulate：着色与 Scene_FS 相同，再预乘并乘深度权重。
// 输出 float4(C * a, a) * w(z, a)，目标 RT 用 Blend One One 累加。
layout(location = 0) in vec3 v2f_NormalVS;
layout(location = 1) in vec3 v2f_PosVS;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_SceneShading
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_LightDirVSAndAmbient;
	vec4 u_LightColor;
	vec4 u_WeightedParams; // x=w1, y=w2, z=e1, w=e2
	vec4 u_FourierParams;  // Fourier OIT 深度窗口与阶数；本阶段不用
};

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) mat4 u_ModelMatrix;
	layout(offset = 64) vec4 u_AlbedoOpacity;
} pc;
#define u_AlbedoOpacity pc.u_AlbedoOpacity
#else
uniform vec4 u_AlbedoOpacity;
#endif

void main()
{
	vec3 albedo = u_AlbedoOpacity.rgb;
	float opacity = u_AlbedoOpacity.a;
	vec3 N = normalize(v2f_NormalVS);
	vec3 L = normalize(u_LightDirVSAndAmbient.xyz);
	float ndotl = abs(dot(N, L));
	vec3 V = normalize(-v2f_PosVS);
	vec3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), 32.0);

	vec3 ambient = albedo * u_LightDirVSAndAmbient.w;
	vec3 diffuse = albedo * ndotl * u_LightColor.rgb;
	vec3 result = ambient + diffuse + vec3(0.18) * spec * u_LightColor.rgb;

	float z = abs(v2f_PosVS.z);
	float w1 = max(u_WeightedParams.x, 1e-5);
	float w2 = max(u_WeightedParams.y, 1e-5);
	float e1 = u_WeightedParams.z;
	float e2 = u_WeightedParams.w;
	float weight = opacity * max(1e-2, min(3e3,
		10.0 / (1e-5 + pow(z / w1, e1) + pow(z / w2, e2))));

	Color_ = vec4(result * opacity, opacity) * weight;
}
