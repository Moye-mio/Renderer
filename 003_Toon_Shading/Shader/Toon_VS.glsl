#version 430 core
layout(location = 0) in vec3 _Position;
layout(location = 1) in vec3 _Normal;
layout(location = 2) in vec2 _TexCoord;
layout(location = 4) in vec3 _OutlineAttr; // x=partIndex，与 OutlineBake 写入的 bitangent 对齐

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_ToonShading
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_LightDirVsAndAmbient;
	vec4 u_LightColor;
	vec4 u_RampParams;
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
layout(location = 1) out vec3 v2f_NormalVs;
layout(location = 2) out float v2f_ViewZ;
layout(location = 3) out float v2f_PartIndex;

void main()
{
	vec4 posVs = u_ViewMatrix * u_ModelMatrix * vec4(_Position, 1.0);
	gl_Position = u_ProjectionMatrix * posVs;
	v2f_TexCoords = _TexCoord;
	// 内线检测用硬边法线，不是描边壳的平滑法线：UV 缝和折边正是要画出来的那些。
	v2f_NormalVs = normalize(mat3(transpose(inverse(u_ViewMatrix * u_ModelMatrix))) * _Normal);
	v2f_ViewZ = max(-posVs.z, 1e-3);
	v2f_PartIndex = _OutlineAttr.x;
}
