#version 430 core

// Weighted Blended OIT Revealage：只写覆盖率 a，不乘权重。
// Blend Zero OneMinusSrcAlpha + 清空 1，等价于连乘 prod(1-a)。
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
	vec4 u_WeightedParams;
	vec4 u_FourierParams;
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
	Color_ = vec4(u_AlbedoOpacity.a);
}
