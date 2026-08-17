#version 430 core
layout(location = 0) in vec3 _Position;
layout(location = 1) in vec3 _Normal;
layout(location = 2) in vec2 _TexCoord;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_SceneShading
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_LightDirVSAndAmbient; // xyz: 视空间朝向光源, w: 环境光
	vec4 u_LightColor;           // rgb: 方向光颜色
	vec4 u_WeightedParams;       // WBOIT 权重；本阶段不用，但同名 block 各阶段声明必须一致
};

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) mat4 u_ModelMatrix;
	layout(offset = 64) vec4 u_AlbedoOpacity;
} pc;
#define u_ModelMatrix pc.u_ModelMatrix
#else
uniform mat4 u_ModelMatrix;
#endif

layout(location = 0) out vec3 v2f_NormalVS;
layout(location = 1) out vec3 v2f_PosVS;

void main()
{
	vec4 posVS = u_ViewMatrix * u_ModelMatrix * vec4(_Position, 1.0);
	gl_Position = u_ProjectionMatrix * posVS;
	v2f_PosVS = posVS.xyz;
	v2f_NormalVS = normalize(mat3(transpose(inverse(u_ViewMatrix * u_ModelMatrix))) * _Normal);
}
