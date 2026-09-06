#version 430 core

layout(location = 0) in vec3 v2f_Normal;
layout(location = 1) flat in uint v2f_ProbeIndex;
layout(location = 0) out vec4 FragColor;

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

LAYOUT_BIND(0, 1) uniform sampler2D u_Irradiance;
LAYOUT_BIND(0, 2) uniform sampler2D u_DepthTexture;

vec2 OctEncode(vec3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	vec2 oct = n.xy;
	if (n.z < 0.0)
		oct = (1.0 - abs(oct.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return oct * 0.5 + 0.5;
}

ivec2 ProbeBase(uint probeIndex)
{
	ivec3 counts = vol.u_ProbeCounts.xyz;
	int ts = vol.u_Atlas.w;
	int x = int(probeIndex % uint(counts.x));
	uint yz = probeIndex / uint(counts.x);
	int y = int(yz % uint(counts.y));
	int z = int(yz / uint(counts.y));
	return ivec2(x * ts, (z * counts.y + y) * ts);
}

void main()
{
	vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(u_DepthTexture, 0));
	float sceneDepth = texture(u_DepthTexture, screenUV).r;
	if (gl_FragCoord.z > sceneDepth)
		discard;

	vec3 n = normalize(v2f_Normal);
	vec2 octa = OctEncode(n);
	float inner = float(vol.u_Atlas.z);
	vec2 local = octa * inner + 1.0;
	vec2 uv = (vec2(ProbeBase(v2f_ProbeIndex)) + local) / vec2(vol.u_Atlas.xy);
	vec3 irr = texture(u_Irradiance, uv).rgb;
	FragColor = vec4(irr, 1.0);
}
