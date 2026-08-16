#version 430 core

// Clustered Forward 的着色结果画在离屏 RT 上（为了复用深度预通道的 D32 做 Early-Z），
// 这里 1:1 拷回 backbuffer。采样器是 Nearest + ClampToEdge。
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
