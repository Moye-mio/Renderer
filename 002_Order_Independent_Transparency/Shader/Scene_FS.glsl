#version 430 core

// 基线着色：视空间方向光 + 环境光。不透明度来自 push constant，
// 不透明管线关掉混合所以 alpha 无影响；半透明管线用 SrcAlpha 混合。
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
	vec4 u_WeightedParams; // WBOIT 权重；本阶段不用，但同名 block 各阶段声明必须一致
	vec4 u_FourierParams;  // Fourier OIT 深度窗口与阶数；同上
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
	// 盒子从内部看、半透明双面都可能是背面，用 abs 避免整面涂黑。
	float ndotl = abs(dot(N, L));
	vec3 V = normalize(-v2f_PosVS);
	vec3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), 32.0);

	vec3 ambient = albedo * u_LightDirVSAndAmbient.w;
	vec3 diffuse = albedo * ndotl * u_LightColor.rgb;
	vec3 result = ambient + diffuse + vec3(0.18) * spec * u_LightColor.rgb;
	Color_ = vec4(result, opacity);
}
