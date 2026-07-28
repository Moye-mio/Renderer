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

// 任务 8：RSMBufferPass 注册了两段 push constant：
//   offset = 0    : u_ModelMatrix   (mat4, 64B)
//   offset = 64   : u_LightVPMatrix (mat4, 64B)
#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0)  mat4 u_ModelMatrix;
	layout(offset = 64) mat4 u_LightVPMatrix;
} pc;
#define u_ModelMatrix   pc.u_ModelMatrix
#define u_LightVPMatrix pc.u_LightVPMatrix
#else
uniform mat4 u_ModelMatrix;
uniform mat4 u_LightVPMatrix;
#endif

// 任务 10：v2f 输出必须显式 layout(location=N)
layout(location = 0) out vec2 v2f_TexCoords;
layout(location = 1) out vec3 v2f_Normal;
layout(location = 2) out vec3 v2f_FragPosInViewSpace;

void main()
{
	vec4 FragPosInWorldSpace = u_ModelMatrix * vec4(_Position, 1.0f);
	gl_Position = u_LightVPMatrix * FragPosInWorldSpace;
	v2f_TexCoords = _TexCoord;
	//存储的是在相机空间下的位置以及法线，不是光源空间下的
	v2f_Normal = normalize(mat3(transpose(inverse(u_ViewMatrix * u_ModelMatrix))) * _Normal);	//这个可以在外面算好了传进来
	v2f_FragPosInViewSpace = vec3(u_ViewMatrix * FragPosInWorldSpace);
}