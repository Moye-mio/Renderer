#version 430 core

// 任务 10：v2f 输入必须显式 layout(location=N)，与 Sponza_VS 的 out location 对齐。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_Normal;
layout(location = 2) in vec3 v2f_FragPosInViewSpace;

layout (location = 0) out vec3 Albedo_;
layout (location = 1) out vec3 Normal_;
layout (location = 2) out vec3 Position_;

// 任务 8：双后端兼容 binding 装饰宏。glslang 编译 Vulkan 时预定义 VULKAN=100。
#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 1) uniform sampler2D u_DiffuseTexture;

void main()
{
	vec3 Albedo = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
	Albedo_ = Albedo;
	Normal_ = v2f_Normal;
	Position_ = v2f_FragPosInViewSpace;
}