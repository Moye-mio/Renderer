#version 430 core

// v2f 输入必须显式 layout(location=N)，与 Sponza_VS 的 out location 对齐。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_Normal;
layout(location = 2) in vec3 v2f_FragPosInViewSpace;

// G-Buffer 三个颜色附件：Albedo / Normal(view) / Position(view)
layout (location = 0) out vec3 Albedo_;
layout (location = 1) out vec3 Normal_;
layout (location = 2) out vec3 Position_;

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
	Normal_ = normalize(v2f_Normal);
	Position_ = v2f_FragPosInViewSpace;
}
