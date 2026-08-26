#version 430 core
// 背面外扩。Clip 空间沿投影法线挤 xy，线宽近似恒定像素。
layout(location = 0) in vec3 _Position;
layout(location = 1) in vec3 _Normal;
layout(location = 2) in vec2 _TexCoord;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_Outline
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_OutlineParams; // x=widthNdc  y=zBias  z/w unused
	vec4 u_OutlineColor;
};

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) mat4 u_ModelMatrix;
} pc;
#define u_ModelMatrix pc.u_ModelMatrix
#else
uniform mat4 u_ModelMatrix;
#endif

void main()
{
	mat4 mv = u_ViewMatrix * u_ModelMatrix;
	vec4 posVS = mv * vec4(_Position, 1.0);
	vec3 nVS = normalize(mat3(transpose(inverse(mv))) * _Normal);

	vec4 clipPos = u_ProjectionMatrix * posVS;
	vec4 clipN = u_ProjectionMatrix * vec4(nVS, 0.0);
	float nLen = length(clipN.xy);
	vec2 nxy = nLen > 1e-5 ? (clipN.xy / nLen) : vec2(0.0);

	// ndc += widthNdc  ⟺  clip.xy += widthNdc * clip.w
	clipPos.xy += nxy * u_OutlineParams.x * clipPos.w;
	// Z∈[0,1]，减小 clip.z 往相机拉，减轻轮廓处 z-fight。
	clipPos.z -= u_OutlineParams.y * clipPos.w;
	gl_Position = clipPos;
}
