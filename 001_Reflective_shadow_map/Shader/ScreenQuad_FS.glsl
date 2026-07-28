#version 430 core

// 任务 10：v2f 输入与片元输出都显式 layout(location=N)
layout(location = 0) in  vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_Texture2D;

void main()
{
	// 方案 A：linear → sRGB 的 gamma 编码统一交给 backbuffer 硬件完成
	//   - VK：swapchain 选用 VK_FORMAT_B8G8R8A8_SRGB，硬件自动编码
	//   - GL：GLFW_SRGB_CAPABLE=TRUE + glEnable(GL_FRAMEBUFFER_SRGB)，驱动自动编码
	// 因此 FS 直接输出 linear 颜色，避免重复 gamma 导致 VK/GL 画面不一致。
	vec3 TexelColor = texture(u_Texture2D, v2f_TexCoords).rgb;
	Color_ = vec4(TexelColor, 1.0f);
}