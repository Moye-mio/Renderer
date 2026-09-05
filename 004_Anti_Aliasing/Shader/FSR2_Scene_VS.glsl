#version 430 core

// FSR 2.0 几何：无 jitter 的投影算 velocity，再把当前 clip.xy 按 Halton 子像素偏移。
// 与 TAA 同一套 jitter / velocity 约定，但画在渲染分辨率上，供显示分辨率累加。
layout(location = 0) in vec3 _Position;
layout(location = 1) in vec3 _Normal;
layout(location = 2) in vec2 _TexCoord;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_FSR2Scene
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	mat4 u_PrevViewProj;
	vec4 u_Jitter; // xy: 当前帧 NDC 子像素偏移（相对渲染分辨率）
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
layout(location = 2) noperspective out vec2 v2f_Velocity;
layout(location = 3) out float v2f_ViewZ;

void main()
{
	vec4 worldPos = u_ModelMatrix * vec4(_Position, 1.0);
	vec4 posVs = u_ViewMatrix * worldPos;
	vec4 currClip = u_ProjectionMatrix * posVs;
	vec4 prevClip = u_PrevViewProj * worldPos;

	vec2 currUV = currClip.xy / max(currClip.w, 1e-6) * 0.5 + 0.5;
	vec2 prevUV = prevClip.xy / max(prevClip.w, 1e-6) * 0.5 + 0.5;
	v2f_Velocity = currUV - prevUV;
	v2f_ViewZ = -posVs.z;

	gl_Position = currClip;
	gl_Position.xy += u_Jitter.xy * currClip.w;

	v2f_TexCoords = _TexCoord;
	v2f_NormalVs = normalize(mat3(transpose(inverse(u_ViewMatrix * u_ModelMatrix))) * _Normal);
}
