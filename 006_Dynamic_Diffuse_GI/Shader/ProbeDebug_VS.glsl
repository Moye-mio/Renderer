#version 430 core

#ifdef VULKAN
#define VERTEX_ID gl_VertexIndex
#define INSTANCE_ID gl_InstanceIndex
#else
#define VERTEX_ID gl_VertexID
#define INSTANCE_ID gl_InstanceID
#endif

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform DDGIVolume
{
	vec4  u_ProbeOrigin;
	vec4  u_ProbeSpacing;
	ivec4 u_ProbeCounts;
	vec4  u_LightDir;
	vec4  u_LightColor;
	vec4  u_SkyColor;
	ivec4 u_Atlas;
	ivec4 u_Frame;
	vec4  u_Bounce;
	vec4  u_RayRot0;
	vec4  u_RayRot1;
	vec4  u_RayRot2;
} vol;

LAYOUT_BIND(0, 3) layout(std140) uniform u_Matrices4ProjectionWorld
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
};

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) float u_ProbeScale;
} pc;
#define u_ProbeScale pc.u_ProbeScale
#else
uniform float u_ProbeScale;
#endif

layout(location = 0) out vec3 v2f_Normal;
layout(location = 1) flat out uint v2f_ProbeIndex;

// 单位立方体 12 三角，按 VERTEX_ID 展开，无需 VBO。
const vec3 kCube[36] = vec3[36](
	vec3(-1,-1,-1), vec3(-1,-1, 1), vec3(-1, 1, 1),
	vec3(-1,-1,-1), vec3(-1, 1, 1), vec3(-1, 1,-1),
	vec3( 1,-1,-1), vec3( 1, 1,-1), vec3( 1, 1, 1),
	vec3( 1,-1,-1), vec3( 1, 1, 1), vec3( 1,-1, 1),
	vec3(-1,-1,-1), vec3( 1,-1,-1), vec3( 1,-1, 1),
	vec3(-1,-1,-1), vec3( 1,-1, 1), vec3(-1,-1, 1),
	vec3(-1, 1,-1), vec3(-1, 1, 1), vec3( 1, 1, 1),
	vec3(-1, 1,-1), vec3( 1, 1, 1), vec3( 1, 1,-1),
	vec3(-1,-1,-1), vec3(-1, 1,-1), vec3( 1, 1,-1),
	vec3(-1,-1,-1), vec3( 1, 1,-1), vec3( 1,-1,-1),
	vec3(-1,-1, 1), vec3( 1,-1, 1), vec3( 1, 1, 1),
	vec3(-1,-1, 1), vec3( 1, 1, 1), vec3(-1, 1, 1)
);

void main()
{
	vec3 local = kCube[VERTEX_ID];
	uint probeIndex = uint(INSTANCE_ID);
	ivec3 counts = vol.u_ProbeCounts.xyz;
	uint x = probeIndex % uint(counts.x);
	uint yz = probeIndex / uint(counts.x);
	uint y = yz % uint(counts.y);
	uint z = yz / uint(counts.y);
	vec3 center = vol.u_ProbeOrigin.xyz + vec3(float(x), float(y), float(z)) * vol.u_ProbeSpacing.xyz;
	vec3 world = center + local * u_ProbeScale;
	gl_Position = u_ProjectionMatrix * u_ViewMatrix * vec4(world, 1.0);
	v2f_Normal = normalize(local);
	v2f_ProbeIndex = probeIndex;
}
