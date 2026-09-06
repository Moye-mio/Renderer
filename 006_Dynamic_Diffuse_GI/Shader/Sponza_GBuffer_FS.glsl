#version 430 core

layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_WorldNormal;
layout(location = 2) in vec3 v2f_WorldPos;

layout(location = 0) out vec3 Albedo_;
layout(location = 1) out vec3 Normal_;
layout(location = 2) out vec3 Position_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 1) uniform sampler2D u_DiffuseTexture;

void main()
{
	Albedo_ = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
	Normal_ = normalize(v2f_WorldNormal);
	Position_ = v2f_WorldPos;
}
