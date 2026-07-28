#version 430 core
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

// 双后端兼容：
//   Vulkan：所有 push constant 必须放进 layout(push_constant) block
//   OpenGL：仍使用顶层 uniform（GL 端 GLCommandList 通过反射 glName 调 glUniformMatrix4fv）
#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) mat4 u_ModelMatrix;
} pc;
#define u_ModelMatrix pc.u_ModelMatrix
#else
uniform mat4 u_ModelMatrix;
#endif

// v2f 输出必须显式 layout(location=N)，Vulkan 强制要求；GL 4.3+ 也兼容。
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
