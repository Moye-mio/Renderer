#version 430 core

// Forward+ 着色 VS：与 Forward_VS 同一套视空间插值。
layout(location = 0) in vec3 _Position;
layout(location = 1) in vec3 _Normal;
layout(location = 2) in vec2 _TexCoord;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_Matrices4ProjectionWorld
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
};

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) mat4 u_ModelMatrix;
} pc;
#define u_ModelMatrix pc.u_ModelMatrix
#else
uniform mat4 u_ModelMatrix;
#endif

layout(location = 0) out vec2 v2f_TexCoords;
layout(location = 1) out vec3 v2f_Normal;
layout(location = 2) out vec3 v2f_FragPosInViewSpace;

void main()
{
	vec4 FragPosInViewSpace = u_ViewMatrix * u_ModelMatrix * vec4(_Position, 1.0f);
	gl_Position = u_ProjectionMatrix * FragPosInViewSpace;
	v2f_TexCoords = _TexCoord;
	v2f_Normal = normalize(mat3(transpose(inverse(u_ViewMatrix * u_ModelMatrix))) * _Normal);
	v2f_FragPosInViewSpace = vec3(FragPosInViewSpace);
}
