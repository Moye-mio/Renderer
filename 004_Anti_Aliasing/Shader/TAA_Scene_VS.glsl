#version 430 core

// TAA 几何：无 jitter 的投影算 velocity，再把当前 clip.xy 按 Halton 子像素偏移。
layout(location = 0) in vec3 _Position;
layout(location = 1) in vec3 _Normal;
layout(location = 2) in vec2 _TexCoord;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_TAAScene
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	mat4 u_PrevViewProj;
	vec4 u_Jitter; // xy: 当前帧 NDC 子像素偏移
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
// UV 空间速度（当前无 jitter UV − 上一帧无 jitter UV），屏幕空间插值。
layout(location = 2) noperspective out vec2 v2f_Velocity;

void main()
{
	vec4 worldPos = u_ModelMatrix * vec4(_Position, 1.0);
	vec4 posVs = u_ViewMatrix * worldPos;
	vec4 currClip = u_ProjectionMatrix * posVs;
	vec4 prevClip = u_PrevViewProj * worldPos;

	vec2 currUV = currClip.xy / max(currClip.w, 1e-6) * 0.5 + 0.5;
	vec2 prevUV = prevClip.xy / max(prevClip.w, 1e-6) * 0.5 + 0.5;
	v2f_Velocity = currUV - prevUV;

	gl_Position = currClip;
	gl_Position.xy += u_Jitter.xy * currClip.w;

	v2f_TexCoords = _TexCoord;
	v2f_NormalVs = normalize(mat3(transpose(inverse(u_ViewMatrix * u_ModelMatrix))) * _Normal);
}
