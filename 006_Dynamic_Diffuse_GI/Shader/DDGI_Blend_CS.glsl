#version 460

// 每个 workgroup 更新一个 probe 的 8×8 八面体内部纹素：对 64 根射线做归约，
// 再和上一帧 atlas 做 hysteresis 混合。
// irradiance 用余弦权重；深度矩用锐化权重（余弦叶太宽会把 mean/variance 抹平，
// Chebyshev 可见性就失效了），并按 maxRayDistance 归一化后再存，避免 fp16 下
// mean2 - mean*mean 的灾难性抵消。
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform DDGIVolume
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

layout(set = 0, binding = 1, std430) readonly buffer RayHits
{
	vec4 u_RayHits[];
};

layout(set = 0, binding = 2) uniform sampler2D u_HistoryIrradiance;
layout(set = 0, binding = 3) uniform sampler2D u_HistoryDistance;
layout(set = 0, binding = 4, rgba16f) uniform image2D u_OutIrradiance;
layout(set = 0, binding = 5, rgba16f) uniform image2D u_OutDistance;

const float PI = 3.14159265359;
const float GOLDEN = 0.61803398875;
// 深度矩的权重指数。64 根射线在球面上平均间距约 14°，取 12 时最近的射线权重仍
// 有 0.4 量级，不会出现整格没有有效样本。
const float DEPTH_SHARPNESS = 12.0;

vec3 SphericalFibonacci(float i, float n)
{
	float phi = 2.0 * PI * fract(i * GOLDEN);
	float cosTheta = 1.0 - (2.0 * i + 1.0) / n;
	float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
	return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// 必须和 DDGI_Trace_CS 完全一致：同一帧的 UBO 里旋转矩阵是同一份。
vec3 ProbeRayDir(uint rayIndex, uint rayCount)
{
	mat3 rot = mat3(vol.u_RayRot0.xyz, vol.u_RayRot1.xyz, vol.u_RayRot2.xyz);
	return normalize(rot * SphericalFibonacci(float(rayIndex), float(rayCount)));
}

vec3 OctDecode(vec2 f)
{
	f = f * 2.0 - 1.0;
	vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	float t = max(-n.z, 0.0);
	n.x += n.x >= 0.0 ? -t : t;
	n.y += n.y >= 0.0 ? -t : t;
	return normalize(n);
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
	uint probeIndex = gl_WorkGroupID.x;
	uint probeCount = uint(vol.u_ProbeCounts.x * vol.u_ProbeCounts.y * vol.u_ProbeCounts.z);
	if (probeIndex >= probeCount)
		return;

	int octRes = vol.u_Atlas.z;
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	if (local.x >= octRes || local.y >= octRes)
		return;

	ivec2 base = ProbeBase(probeIndex);
	ivec2 texel = base + local + ivec2(1);

	vec2 octaUV = (vec2(local) + 0.5) / float(octRes);
	vec3 texelDir = OctDecode(octaUV);

	uint rays = uint(vol.u_ProbeCounts.w);
	float invMaxDist = 1.0 / max(vol.u_ProbeSpacing.w, 1e-4);

	vec3 sumL = vec3(0.0);
	float sumW = 0.0;
	float sumD = 0.0;
	float sumD2 = 0.0;
	float sumWD = 0.0;
	for (uint i = 0u; i < rays; ++i)
	{
		float c = max(dot(texelDir, ProbeRayDir(i, rays)), 0.0);
		if (c <= 0.0)
			continue;
		vec4 hit = u_RayHits[probeIndex * rays + i];
		sumL += hit.rgb * c;
		sumW += c;

		// 归一化到 [0,1] 再入矩，fp16 下 mean/mean2 的有效位数好得多。
		float d = min(hit.w, vol.u_ProbeSpacing.w) * invMaxDist;
		float wd = pow(c, DEPTH_SHARPNESS);
		sumD += d * wd;
		sumD2 += d * d * wd;
		sumWD += wd;
	}

	vec3 irr = (sumW > 1e-5) ? (sumL / sumW) : vec3(0.0);
	float mean = (sumWD > 1e-8) ? (sumD / sumWD) : 1.0;
	float mean2 = (sumWD > 1e-8) ? (sumD2 / sumWD) : 1.0;

	float hyst = vol.u_LightColor.w;
	if (vol.u_Frame.z == 0)
	{
		ivec2 histSize = textureSize(u_HistoryIrradiance, 0);
		vec2 histUV = (vec2(texel) + 0.5) / vec2(histSize);
		vec3 histIrr = texture(u_HistoryIrradiance, histUV).rgb;
		vec2 histDist = texture(u_HistoryDistance, histUV).rg;
		irr = mix(irr, histIrr, hyst);
		mean = mix(mean, histDist.x, hyst);
		mean2 = mix(mean2, histDist.y, hyst);
	}

	imageStore(u_OutIrradiance, texel, vec4(irr, 1.0));
	imageStore(u_OutDistance, texel, vec4(mean, mean2, 0.0, 1.0));
}
