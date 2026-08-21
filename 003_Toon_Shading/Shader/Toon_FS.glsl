#version 430 core

// M1：只采样 Diffuse + 视空间方向光。Ramp / ilm 留给 M2。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_NormalVS;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_ToonShading
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_LightDirVSAndAmbient;
	vec4 u_LightColor;
};

LAYOUT_BIND(0, 1) uniform sampler2D u_DiffuseTexture;

void main()
{
	vec3 albedo = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
	vec3 N = normalize(v2f_NormalVS);
	vec3 L = normalize(u_LightDirVSAndAmbient.xyz);
	float ndotl = max(dot(N, L), 0.0);

	vec3 ambient = albedo * u_LightDirVSAndAmbient.w;
	vec3 diffuse = albedo * ndotl * u_LightColor.rgb;
	Color_ = vec4(ambient + diffuse, 1.0);
}
