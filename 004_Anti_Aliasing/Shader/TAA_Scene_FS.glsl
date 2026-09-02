#version 430 core

// TAA 前向着色：漫反射 ×（环境 + Lambert）+ UV 空间 motion vector。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_NormalVs;
layout(location = 2) noperspective in vec2 v2f_Velocity;

layout(location = 0) out vec4 Color_;
layout(location = 1) out vec2 Velocity_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 2) layout(std140) uniform u_Shading
{
	vec4 u_LightDirVsAndAmbient; // xyz: 视空间指向光源, w: 环境光
	vec4 u_LightColor;           // rgb: 主光色
};

LAYOUT_BIND(0, 1) uniform sampler2D u_DiffuseTexture;

void main()
{
	vec3 albedo = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
	vec3 N = normalize(v2f_NormalVs);
	vec3 L = normalize(u_LightDirVsAndAmbient.xyz);
	float ambient = u_LightDirVsAndAmbient.w;
	float ndotl = max(dot(N, L), 0.0);
	vec3 result = albedo * (ambient + u_LightColor.rgb * ndotl);
	Color_ = vec4(result, 1.0);
	Velocity_ = v2f_Velocity;
}
