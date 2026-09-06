#version 460
#extension GL_EXT_ray_query : require

// 每个 workgroup 更新一个 probe：64 根球面 Fibonacci 射线（整套方向按本帧的
// 随机旋转矩阵转过一次），rayQuery 求交后用命中三角形插值烘好的 albedo，
// 再做一次太阳光（带阴影射线）+ 回采上一帧 irradiance 得到后续弹射。
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform accelerationStructureEXT u_TLAS;

layout(set = 0, binding = 1, std140) uniform DDGIVolume
{
	vec4  u_ProbeOrigin;     // xyz, normalBias
	vec4  u_ProbeSpacing;    // xyz, maxRayDistance
	ivec4 u_ProbeCounts;     // xyz, raysPerProbe
	vec4  u_LightDir;        // xyz (指向光源), w = intensity
	vec4  u_LightColor;      // rgb, hysteresis
	vec4  u_SkyColor;        // rgb, giIntensity
	ivec4 u_Atlas;           // xy atlas, octRes, probeTexelSize
	ivec4 u_Frame;           // frameIndex, viewMode, reset, shadowMaskValid
	vec4  u_Bounce;          // x = bounceScale, yzw 保留
	vec4  u_RayRot0;         // 本帧射线集旋转矩阵，xyz 为一列
	vec4  u_RayRot1;
	vec4  u_RayRot2;
} vol;

struct GpuVertex
{
	vec4 posAlbedoR;
	vec4 nrmAlbedoG;
	vec4 uvAlbedoB;
};

layout(set = 0, binding = 2, std430) buffer RayHits
{
	vec4 u_RayHits[]; // rgb radiance, w distance
};

layout(set = 0, binding = 3, std430) readonly buffer Vertices
{
	GpuVertex u_Vertices[];
};

layout(set = 0, binding = 4, std430) readonly buffer Indices
{
	uint u_Indices[];
};

layout(set = 0, binding = 5, std430) readonly buffer MeshRanges
{
	uvec4 u_MeshRanges[]; // firstIndex, indexCount, firstVertex, vertexCount
};

// 上一帧的 probe 场，用于命中点的后续弹射。
layout(set = 0, binding = 6) uniform sampler2D u_PrevIrradiance;
layout(set = 0, binding = 7) uniform sampler2D u_PrevDistance;

const float PI = 3.14159265359;
const float GOLDEN = 0.61803398875;

vec3 SphericalFibonacci(float i, float n)
{
	float phi = 2.0 * PI * fract(i * GOLDEN);
	float cosTheta = 1.0 - (2.0 * i + 1.0) / n;
	float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
	return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// 整套 Fibonacci 方向乘本帧旋转：仰角分布不再逐帧固定，时域累积才是无偏的。
vec3 ProbeRayDir(uint rayIndex, uint rayCount)
{
	mat3 rot = mat3(vol.u_RayRot0.xyz, vol.u_RayRot1.xyz, vol.u_RayRot2.xyz);
	return normalize(rot * SphericalFibonacci(float(rayIndex), float(rayCount)));
}

vec3 ProbeWorldPos(uint probeIndex)
{
	ivec3 counts = vol.u_ProbeCounts.xyz;
	uint x = probeIndex % uint(counts.x);
	uint yz = probeIndex / uint(counts.x);
	uint y = yz % uint(counts.y);
	uint z = yz / uint(counts.y);
	return vol.u_ProbeOrigin.xyz + vec3(float(x), float(y), float(z)) * vol.u_ProbeSpacing.xyz;
}

// ---------------------------------------------------------------------------
// 上一帧 probe 场采样（和 DDGI_Shade_FS 同一套八面体寻址 / Chebyshev）
// ---------------------------------------------------------------------------
vec2 OctEncode(vec3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	vec2 oct = n.xy;
	if (n.z < 0.0)
		oct = (1.0 - abs(oct.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return oct * 0.5 + 0.5;
}

vec2 ProbeAtlasUV(ivec3 idx, vec3 dir)
{
	int ts = vol.u_Atlas.w;
	vec2 base = vec2(idx.x * ts, (idx.z * vol.u_ProbeCounts.y + idx.y) * ts);
	vec2 local = OctEncode(dir) * float(vol.u_Atlas.z) + 1.0;
	return (base + local) / vec2(vol.u_Atlas.xy);
}

// moments 与 dist 都是「按 maxRayDistance 归一化」后的量，见 DDGI_Blend_CS。
float Chebyshev(vec2 moments, float dist)
{
	if (dist <= moments.x)
		return 1.0;
	float variance = max(moments.y - moments.x * moments.x, 1e-6);
	float d = dist - moments.x;
	float p = variance / (variance + d * d);
	return clamp((p - 0.1) / 0.9, 0.0, 1.0);
}

vec3 SamplePrevIrradiance(vec3 worldPos, vec3 normal)
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
		vec2 moments = texture(u_PrevDistance, uv).rg;

		vec3 trilinear = mix(vec3(1.0) - frac, frac, vec3(x, y, z));
		float w = trilinear.x * trilinear.y * trilinear.z;
		w *= Chebyshev(moments, dist * invMaxDist);
		w *= max(0.05, dot(normal, normalize(probePos - worldPos)));
		sumIrr += texture(u_PrevIrradiance, uv).rgb * w;
		sumW += w;
	}
	return (sumW > 1e-5) ? (sumIrr / sumW) : vec3(0.0);
}

bool TraceShadow(vec3 origin, vec3 dir, float tmax)
{
	rayQueryEXT rq;
	uint flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
	rayQueryInitializeEXT(rq, u_TLAS, flags, 0xFF, origin, 0.002, dir, tmax);
	while (rayQueryProceedEXT(rq)) {}
	return rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

vec3 ShadeHit(vec3 albedo, vec3 normal, vec3 hitPos)
{
	vec3 L = normalize(vol.u_LightDir.xyz);
	float ndotl = max(dot(normal, L), 0.0);
	vec3 direct = albedo * ndotl * vol.u_LightColor.rgb * vol.u_LightDir.w;
	if (ndotl > 0.0 && TraceShadow(hitPos + normal * 0.004, L, vol.u_ProbeSpacing.w))
		direct = vec3(0.0);

	// 后续弹射：命中点回采上一帧的 probe 场。反馈增益 = albedo * bounceScale，
	// bounceScale < 1 保证反复迭代收敛而不是自激发散。reset 帧没有历史可用。
	vec3 bounce = vec3(0.0);
	if (vol.u_Frame.z == 0)
		bounce = albedo * SamplePrevIrradiance(hitPos, normal) * vol.u_Bounce.x;

	return direct + bounce;
}

void main()
{
	uint probeIndex = gl_WorkGroupID.x;
	uint rayIndex = gl_LocalInvocationID.x;
	uint rays = uint(vol.u_ProbeCounts.w);
	uint probeCount = uint(vol.u_ProbeCounts.x * vol.u_ProbeCounts.y * vol.u_ProbeCounts.z);
	if (probeIndex >= probeCount || rayIndex >= rays)
		return;

	vec3 origin = ProbeWorldPos(probeIndex);
	vec3 dir = ProbeRayDir(rayIndex, rays);
	float tmax = vol.u_ProbeSpacing.w;

	rayQueryEXT rq;
	rayQueryInitializeEXT(rq, u_TLAS, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.02, dir, tmax);
	while (rayQueryProceedEXT(rq)) {}

	vec3 radiance = vol.u_SkyColor.rgb;
	float dist = tmax;
	if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
	{
		dist = rayQueryGetIntersectionTEXT(rq, true);
		uint meshId = rayQueryGetIntersectionGeometryIndexEXT(rq, true);
		uint prim = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
		vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
		uvec4 range = u_MeshRanges[meshId];
		uint i0 = u_Indices[range.x + prim * 3u + 0u];
		uint i1 = u_Indices[range.x + prim * 3u + 1u];
		uint i2 = u_Indices[range.x + prim * 3u + 2u];
		GpuVertex v0 = u_Vertices[range.z + i0];
		GpuVertex v1 = u_Vertices[range.z + i1];
		GpuVertex v2 = u_Vertices[range.z + i2];

		float w0 = 1.0 - bary.x - bary.y;
		float w1 = bary.x;
		float w2 = bary.y;
		vec3 albedo = vec3(
			v0.posAlbedoR.w * w0 + v1.posAlbedoR.w * w1 + v2.posAlbedoR.w * w2,
			v0.nrmAlbedoG.w * w0 + v1.nrmAlbedoG.w * w1 + v2.nrmAlbedoG.w * w2,
			v0.uvAlbedoB.z  * w0 + v1.uvAlbedoB.z  * w1 + v2.uvAlbedoB.z  * w2);
		vec3 n = normalize(
			v0.nrmAlbedoG.xyz * w0 + v1.nrmAlbedoG.xyz * w1 + v2.nrmAlbedoG.xyz * w2);
		if (dot(n, dir) > 0.0)
			n = -n;
		vec3 hitPos = origin + dir * dist;
		radiance = ShadeHit(albedo, n, hitPos);
	}

	u_RayHits[probeIndex * rays + rayIndex] = vec4(radiance, dist);
}
