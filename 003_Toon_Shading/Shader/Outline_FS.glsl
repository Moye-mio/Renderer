#version 430 core
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_Outline
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_OutlineParams;
	vec4 u_OutlineColor;
};

void main()
{
	Color_ = vec4(u_OutlineColor.rgb, 1.0);
}
