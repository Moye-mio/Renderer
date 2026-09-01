#version 430 core

// 硬件 ResolveTexture 之后的 1-sample 颜色 1:1 拷回 backbuffer。
layout(location = 0) in  vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_ShadeColor;

void main()
{
	Color_ = texture(u_ShadeColor, v2f_TexCoords);
}
