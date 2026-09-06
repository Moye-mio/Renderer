#version 460

// 每个线程拷一个 probe 的八面体 1 像素边框，给后续双线性采样缝合对面。
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

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

layout(set = 0, binding = 1, rgba16f) uniform image2D u_Irradiance;
layout(set = 0, binding = 2, rgba16f) uniform image2D u_Distance;

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

void CopyTexel(ivec2 dst, ivec2 src)
{
	imageStore(u_Irradiance, dst, imageLoad(u_Irradiance, src));
	imageStore(u_Distance, dst, imageLoad(u_Distance, src));
}

void main()
{
	uint probeIndex = gl_WorkGroupID.x;
	uint probeCount = uint(vol.u_ProbeCounts.x * vol.u_ProbeCounts.y * vol.u_ProbeCounts.z);
	if (probeIndex >= probeCount)
		return;

	int n = vol.u_Atlas.z;
	ivec2 base = ProbeBase(probeIndex);

	for (int i = 1; i <= n; ++i)
	{
		CopyTexel(base + ivec2(i, 0),     base + ivec2(n + 1 - i, 1));
		CopyTexel(base + ivec2(i, n + 1), base + ivec2(n + 1 - i, n));
		CopyTexel(base + ivec2(0, i),     base + ivec2(1, n + 1 - i));
		CopyTexel(base + ivec2(n + 1, i), base + ivec2(n, n + 1 - i));
	}

	CopyTexel(base + ivec2(0, 0),         base + ivec2(n, n));
	CopyTexel(base + ivec2(n + 1, 0),     base + ivec2(1, n));
	CopyTexel(base + ivec2(0, n + 1),     base + ivec2(n, 1));
	CopyTexel(base + ivec2(n + 1, n + 1), base + ivec2(1, 1));
}
