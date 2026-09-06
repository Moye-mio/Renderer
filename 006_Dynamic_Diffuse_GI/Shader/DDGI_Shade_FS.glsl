#version 430 core

layout(location = 0) in vec2 v2f_TexCoords;
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

LAYOUT_BIND(0, 1) uniform sampler2D u_AlbedoTexture;
LAYOUT_BIND(0, 2) uniform sampler2D u_NormalTexture;
LAYOUT_BIND(0, 3) uniform sampler2D u_PositionTexture;
LAYOUT_BIND(0, 4) uniform sampler2D u_Irradiance;
LAYOUT_BIND(0, 5) uniform sampler2D u_Distance;
LAYOUT_BIND(0, 6) uniform sampler2D u_ShadowMask;

vec2 OctEncode(vec3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	vec2 oct = n.xy;
	if (n.z < 0.0)
		oct = (1.0 - abs(oct.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return oct * 0.5 + 0.5;
}

ivec2 ProbeBase(ivec3 idx)
{
	int ts = vol.u_Atlas.w;
	return ivec2(idx.x * ts, (idx.z * vol.u_ProbeCounts.y + idx.y) * ts);
}

vec2 ProbeAtlasUV(ivec3 idx, vec3 dir)
{
	vec2 octa = OctEncode(dir);
	float inner = float(vol.u_Atlas.z);
	vec2 local = octa * inner + 1.0;
	vec2 base = vec2(ProbeBase(idx));
	return (base + local) / vec2(vol.u_Atlas.xy);
}

// moments 与 dist 都已按 maxRayDistance 归一化，见 DDGI_Blend_CS。
float Chebyshev(vec2 moments, float dist)
{
	if (dist <= moments.x)
		return 1.0;
	float variance = max(moments.y - moments.x * moments.x, 1e-6);
	float d = dist - moments.x;
	float p = variance / (variance + d * d);
	return clamp((p - 0.1) / 0.9, 0.0, 1.0);
}

vec3 SampleDDGI(vec3 worldPos, vec3 normal)
{
	float invMaxDist = 1.0 / max(vol.u_ProbeSpacing.w, 1e-4);
	vec3 biased = worldPos + normal * vol.u_ProbeOrigin.w;
	vec3 rel = (biased - vol.u_ProbeOrigin.xyz) / vol.u_ProbeSpacing.xyz;
	ivec3 base = ivec3(floor(rel));
	vec3 frac = fract(rel);
	ivec3 maxIdx = vol.u_ProbeCounts.xyz - ivec3(1);

	vec3 sumIrr = vec3(0.0);
	float sumW = 0.0;
	for (int z = 0; z < 2; ++z)
	for (int y = 0; y < 2; ++y)
	for (int x = 0; x < 2; ++x)
	{
		ivec3 idx = clamp(base + ivec3(x, y, z), ivec3(0), maxIdx);
		vec3 probePos = vol.u_ProbeOrigin.xyz + vec3(idx) * vol.u_ProbeSpacing.xyz;
		vec3 toPoint = biased - probePos;
		float dist = length(toPoint);
		vec3 dir = (dist > 1e-4) ? (toPoint / dist) : normal;

		vec2 uv = ProbeAtlasUV(idx, dir);
		vec3 irr = texture(u_Irradiance, uv).rgb;
		vec2 moments = texture(u_Distance, uv).rg;
		float vis = Chebyshev(moments, dist * invMaxDist);

		vec3 trilinear = mix(vec3(1.0) - frac, frac, vec3(x, y, z));
		float w = trilinear.x * trilinear.y * trilinear.z;
		w *= vis;
		w *= max(0.05, dot(normal, normalize(probePos - worldPos)));
		sumIrr += irr * w;
		sumW += w;
	}
	return (sumW > 1e-5) ? (sumIrr / sumW) : vec3(0.0);
}

void main()
{
	vec3 albedo = texture(u_AlbedoTexture, v2f_TexCoords).rgb;
	vec3 normal = texture(u_NormalTexture, v2f_TexCoords).rgb;
	vec3 worldPos = texture(u_PositionTexture, v2f_TexCoords).rgb;

	if (dot(normal, normal) < 1e-4)
	{
		FragColor = vec4(vol.u_SkyColor.rgb * 0.35, 1.0);
		return;
	}
	normal = normalize(normal);

	// mask 由 DDGI_Shadow_CS 写；没有 ray query 时它没被写过，退回无阴影。
	float shadow = (vol.u_Frame.w != 0) ? texture(u_ShadowMask, v2f_TexCoords).r : 1.0;

	vec3 L = normalize(vol.u_LightDir.xyz);
	vec3 direct = albedo * max(dot(normal, L), 0.0) * vol.u_LightColor.rgb * vol.u_LightDir.w * shadow;
	vec3 gi = albedo * SampleDDGI(worldPos, normal) * vol.u_SkyColor.w;

	int viewMode = vol.u_Frame.y;
	vec3 color = direct + gi;
	if (viewMode == 1)
		color = direct;
	else if (viewMode == 2)
		color = gi;
	else if (viewMode == 3)
		color = albedo;
	else if (viewMode == 4)
		color = normal * 0.5 + 0.5;

	FragColor = vec4(color, 1.0);
}
